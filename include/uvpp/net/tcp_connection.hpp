#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/detail/owner_close.hpp"
#include "uvpp/detail/stream_io.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/buffer.hpp"

namespace uv {

class tcp_listener;
class tcp_connection;
class tcp_connection_view;
class pipe_connection;

namespace co {
class resource_scope;
}

namespace detail {

// This access token is lifetime bookkeeping for a borrowed high-level view; it
// does not own the TCP resource. resource_scope clears connection before it
// destroys its sole tcp_connection owner.
struct tcp_connection_access {
  tcp_connection *connection = nullptr;
};

[[nodiscard]] tcp_connection_view make_tcp_connection_view(
    std::shared_ptr<tcp_connection_access>) noexcept;

struct tcp_connection_state {
  uv_connect_t connect{};
  uv_tcp_t tcp{};
  uv::loop *loop = nullptr;
  std::coroutine_handle<> connect_continuation{};
  std::unique_ptr<tcp_connection_state> *provisional_owner = nullptr;
  int *connect_status_destination = nullptr;
  bool *close_completion_destination = nullptr;
  int connect_status = 0;
  bool initialized = false;
  async_close_state close{};
  stream_io_state io{};
  bool handle_export_active = false;

  static tcp_connection_state &from_connect(uv_connect_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_connection_state *>(bytes - offsetof(tcp_connection_state, connect));
  }

  static tcp_connection_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_connection_state *>(bytes - offsetof(tcp_connection_state, tcp));
  }

  bool closing() const noexcept { return close.closing(); }

  bool has_active_operation() const noexcept {
    return io.write_active || io.active_read != nullptr || handle_export_active;
  }

  uv_stream_t *stream_handle() noexcept {
    return reinterpret_cast<uv_stream_t *>(&tcp);
  }

  uv::loop &execution_loop() noexcept {
    assert(loop != nullptr);
    return *loop;
  }

  stream_io_state &io_state() noexcept { return io; }

  static int acquire_handle_export(void *opaque) noexcept {
    auto &self = *static_cast<tcp_connection_state *>(opaque);
    if (self.closing()) {
      return UV_EBADF;
    }
    if (self.handle_export_active) {
      return UV_EBUSY;
    }
    self.handle_export_active = true;
    return 0;
  }

  static void release_handle_export(void *opaque) noexcept {
    auto &self = *static_cast<tcp_connection_state *>(opaque);
    assert(self.handle_export_active);
    self.handle_export_active = false;
  }

  // Starts uv_close exactly once. This path never allocates and is suitable for
  // owner destruction and native C callbacks.
  bool request_close() noexcept {
    if (!close.begin()) {
      return false;
    }
    uv_close(reinterpret_cast<uv_handle_t *>(&tcp), &tcp_connection_state::on_close);
    return true;
  }

  // Connect/accept failure paths originate in libuv callbacks and therefore
  // cannot allocate. One state-owned continuation slot is sufficient there.
  void request_close_from_callback(std::coroutine_handle<> continuation) noexcept {
    close.set_callback_waiter(continuation);
    (void)request_close();
  }

  // The high-level owner relinquishes its stable state only here. If close has
  // already completed inside its callback, deletion waits until every waiter
  // has been delivered.
  void release_owner() noexcept {
    close.owner_released();
    if (close.open()) {
      (void)request_close();
      return;
    }
    if (close.can_destroy_now()) {
      delete this;
    }
  }

  static void on_connect(uv_connect_t *raw, int status) noexcept {
    auto &self = from_connect(raw);
    self.connect_status = status;
    assert(self.connect_status_destination != nullptr);
    *self.connect_status_destination = status;
    auto continuation = std::exchange(self.connect_continuation, {});
    if (status < 0) {
      assert(self.provisional_owner != nullptr);
      auto *owned = self.provisional_owner->release();
      assert(owned == &self);
      self.release_owner();
      self.request_close_from_callback(continuation);
      return;
    }
    continuation.resume();
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    if (self.close_completion_destination != nullptr) {
      *self.close_completion_destination = true;
      self.close_completion_destination = nullptr;
    }
    self.close.complete([&self]() noexcept { delete &self; });
  }

};

static_assert(std::is_standard_layout_v<tcp_connection_state>);

using tcp_close_completion = owner_close_awaiter<tcp_connection_state, false>;
using tcp_close_result = owner_close_awaiter<tcp_connection_state, true>;

[[nodiscard]] tcp_close_completion close_completion(tcp_connection &) noexcept;
[[nodiscard]] tcp_close_result close_result(tcp_connection &) noexcept;

} // namespace detail

// Experimental v3 movable TCP client owner. Moving transfers the stable state;
// it never relocates the uv_tcp_t. Destruction starts an internal asynchronous
// close and releases the state only from the close callback.
class tcp_connection {
public:
  tcp_connection(const tcp_connection &) = delete;
  tcp_connection &operator=(const tcp_connection &) = delete;

  tcp_connection(tcp_connection &&other) noexcept : state_{std::move(other.state_)} {}

  tcp_connection &operator=(tcp_connection &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  ~tcp_connection() { reset(); }

  uv_tcp_t *native() noexcept { return state_ ? &state_->tcp : nullptr; }
  const uv_tcp_t *native() const noexcept { return state_ ? &state_->tcp : nullptr; }
  uv_handle_t *native_handle() noexcept { return reinterpret_cast<uv_handle_t *>(native()); }
  const uv_handle_t *native_handle() const noexcept {
    return reinterpret_cast<const uv_handle_t *>(native());
  }
  uv_stream_t *native_stream() noexcept { return reinterpret_cast<uv_stream_t *>(native()); }
  const uv_stream_t *native_stream() const noexcept {
    return reinterpret_cast<const uv_stream_t *>(native());
  }
  bool closing() const noexcept { return state_ && state_->closing(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }
  bool has_active_operation() const noexcept {
    return state_ != nullptr && state_->has_active_operation();
  }

  [[nodiscard]] detail::tcp_close_completion close() & noexcept {
    return detail::tcp_close_completion{state_.get(), true};
  }
  detail::tcp_close_completion close() && = delete;

  void request_close() {
    if (!state_) {
      throw_if_error(UV_EBADF);
    }
    if (!state_->close.closed() && state_->has_active_operation()) {
      throw_if_error(UV_EBUSY);
    }
    (void)state_->request_close();
  }

  using read_some_result = detail::stream_read_some_result;
  using read_awaiter = detail::stream_read_awaiter<detail::tcp_connection_state>;

  // Borrows buffer storage until data, EOF, or an error has stopped the read.
  [[nodiscard]] read_awaiter read_some(std::span<std::byte> buffer) noexcept {
    return read_awaiter{state_.get(), buffer};
  }

  using write_awaiter = detail::stream_write_awaiter<detail::tcp_connection_state>;

  // Borrows data until the native write completion invokes the awaiting task.
  // The caller must keep it alive, address-stable, and unmodified until then.
  [[nodiscard]] write_awaiter write(std::string_view data) {
    detail::check_buffer_length(data.size());
    return write_awaiter{state_.get(), data};
  }

  class connect_awaiter {
  public:
    explicit connect_awaiter(const ipv4 &address) noexcept {
      std::memcpy(&peer_, address.native(), sizeof(sockaddr_in));
    }

    explicit connect_awaiter(const ipv6 &address) noexcept {
      std::memcpy(&peer_, address.native(), sizeof(sockaddr_in6));
    }

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (continuation.promise().stop_requested()) {
        state_ = std::make_unique<detail::tcp_connection_state>();
        status_ = UV_ECANCELED;
        return false;
      }
      state_ = std::make_unique<detail::tcp_connection_state>();
      auto &state = *state_;
      state.loop = &continuation.promise().execution_loop();
      state.connect_continuation = continuation;
      state.provisional_owner = &state_;
      state.connect_status_destination = &status_;
      status_ = uv_tcp_init(state.loop->native(), &state.tcp);
      state.connect_status = status_;
      if (status_ < 0) {
        return false;
      }
      state.initialized = true;
      status_ = uv_tcp_connect(&state.connect, &state.tcp,
          reinterpret_cast<const sockaddr *>(&peer_), &detail::tcp_connection_state::on_connect);
      state.connect_status = status_;
      if (status_ < 0) {
        auto resume = std::exchange(state.connect_continuation, {});
        auto *owned = state_.release();
        assert(owned == &state);
        state.release_owner();
        state.request_close_from_callback(resume);
      }
      return true;
    }

    tcp_connection await_resume() {
      throw_if_error(status_);
      state_->provisional_owner = nullptr;
      state_->connect_status_destination = nullptr;
      return tcp_connection{std::move(state_)};
    }

  private:
    sockaddr_storage peer_{};
    std::unique_ptr<detail::tcp_connection_state> state_{};
    int status_ = 0;
  };

  [[nodiscard]] static connect_awaiter connect(const ipv4 &address) noexcept {
    return connect_awaiter{address};
  }

  [[nodiscard]] static connect_awaiter connect(const ipv6 &address) noexcept {
    return connect_awaiter{address};
  }

private:
  explicit tcp_connection(std::unique_ptr<detail::tcp_connection_state> state) noexcept
    : state_{std::move(state)} {}

  void reset() noexcept {
    if (!state_) {
      return;
    }
    auto *state = state_.release();
    assert(!state->io.write_active);
    assert(state->io.active_read == nullptr);
    assert(!state->handle_export_active);
    if (state->io.write_active || state->io.active_read != nullptr ||
        state->handle_export_active) {
      std::terminate();
    }
    state->release_owner();
  }

  std::unique_ptr<detail::tcp_connection_state> state_{};

  friend class tcp_listener;
  friend class tcp_connection_view;
  friend class pipe_connection;
  friend detail::tcp_close_completion detail::close_completion(tcp_connection &) noexcept;
  friend detail::tcp_close_result detail::close_result(tcp_connection &) noexcept;
};

// A non-owning TCP facade produced by resource_scope. It retains only an access
// token, never the connection owner or its native storage. Operations diagnose
// use after resource_scope::finish() before touching native state.
class tcp_connection_view {
public:
  [[nodiscard]] tcp_connection::read_awaiter read_some(
      std::span<std::byte> buffer) const {
    return connection().read_some(buffer);
  }

  [[nodiscard]] tcp_connection::write_awaiter write(std::string_view data) const {
    return connection().write(data);
  }

private:
  explicit tcp_connection_view(std::shared_ptr<detail::tcp_connection_access> access) noexcept
    : access_{std::move(access)} {}

  tcp_connection &connection() const {
    if (!access_ || access_->connection == nullptr) {
      throw std::logic_error{"uv::tcp_connection_view used after resource cleanup"};
    }
    return *access_->connection;
  }

  std::shared_ptr<detail::tcp_connection_access> access_{};

  friend tcp_connection_view detail::make_tcp_connection_view(
      std::shared_ptr<detail::tcp_connection_access>) noexcept;
};

namespace detail {

[[nodiscard]] inline tcp_connection_view make_tcp_connection_view(
    std::shared_ptr<tcp_connection_access> access) noexcept {
  return tcp_connection_view{std::move(access)};
}

[[nodiscard]] inline tcp_close_completion close_completion(
    tcp_connection &connection) noexcept {
  return tcp_close_completion{connection.state_.get(), false};
}

[[nodiscard]] inline tcp_close_result close_result(tcp_connection &connection) noexcept {
  return tcp_close_result{connection.state_.get(), true};
}

} // namespace detail

namespace ops {

[[nodiscard]] inline detail::tcp_close_result close(tcp_connection &connection) noexcept {
  return detail::close_result(connection);
}

} // namespace ops

static_assert(std::is_standard_layout_v<tcp_connection::write_awaiter>);

} // namespace uv
