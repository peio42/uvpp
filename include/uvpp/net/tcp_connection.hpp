#pragma once

#include <concepts>
#include <coroutine>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/buffer.hpp"

namespace uv {

class tcp_listener;
class tcp_connection;
class tcp_connection_view;

namespace co {
class resource_scope;
}

namespace detail {

enum class tcp_close_phase {
  open,
  closing,
  closed,
};

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
  int connect_status = 0;
  bool initialized = false;
  tcp_close_phase close_phase = tcp_close_phase::open;
  std::vector<std::coroutine_handle<>> close_waiters{};
  std::coroutine_handle<> callback_close_waiter{};
  bool owner_released = false;
  bool close_callback_active = false;
  bool write_active = false;
  void *active_read = nullptr;

  static tcp_connection_state &from_connect(uv_connect_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_connection_state *>(bytes - offsetof(tcp_connection_state, connect));
  }

  static tcp_connection_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_connection_state *>(bytes - offsetof(tcp_connection_state, tcp));
  }

  bool closing() const noexcept { return close_phase != tcp_close_phase::open; }

  // Starts uv_close exactly once. This path never allocates and is suitable for
  // owner destruction and native C callbacks.
  bool request_close() noexcept {
    if (close_phase == tcp_close_phase::closed) {
      return false;
    }
    if (close_phase == tcp_close_phase::closing) {
      return false;
    }
    close_phase = tcp_close_phase::closing;
    uv_close(reinterpret_cast<uv_handle_t *>(&tcp), &tcp_connection_state::on_close);
    return true;
  }

  // Called from coroutine suspension, where allocation failure has the normal
  // coroutine exception path. The state owns the continuation value before any
  // user code can run from the close callback.
  bool request_close_and_join(std::coroutine_handle<> continuation) {
    // tcp_close_completion checks this phase before calling us. In particular,
    // a close completion observed from user code resumed by on_close() must not
    // register a new waiter into a callback that is already delivering.
    assert(close_phase != tcp_close_phase::closed);
    if (close_phase == tcp_close_phase::closed) {
      return false;
    }
    close_waiters.push_back(continuation);
    return request_close();
  }

  // Connect/accept failure paths originate in libuv callbacks and therefore
  // cannot allocate. One state-owned continuation slot is sufficient there.
  void request_close_from_callback(std::coroutine_handle<> continuation) noexcept {
    assert(!callback_close_waiter);
    callback_close_waiter = continuation;
    (void)request_close();
  }

  // The high-level owner relinquishes its stable state only here. If close has
  // already completed inside its callback, deletion waits until every waiter
  // has been delivered.
  void release_owner() noexcept {
    assert(!owner_released);
    owner_released = true;
    if (close_phase == tcp_close_phase::open) {
      (void)request_close();
      return;
    }
    if (close_phase == tcp_close_phase::closed && !close_callback_active) {
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
    self.close_phase = tcp_close_phase::closed;
    self.close_callback_active = true;
    auto callback_waiter = std::exchange(self.callback_close_waiter, {});
    // Detach all frame-facing continuations before the first user resumption.
    // The local vector owns only handle values, so later delivery never reads
    // linkage or registration storage from another pending coroutine frame.
    auto waiters = std::move(self.close_waiters);
    if (callback_waiter) {
      callback_waiter.resume();
    }
    for (auto continuation : waiters) {
      if (continuation) {
        continuation.resume();
      }
    }
    self.close_callback_active = false;
    if (self.owner_released) {
      delete &self;
    }
  }

};

static_assert(std::is_standard_layout_v<tcp_connection_state>);

// Internal close-completion primitive. Its state pointer is borrowed from an
// owner that must remain alive through completion (or release ownership through
// tcp_connection_state::release_owner()). It is intentionally not a public
// tcp_connection::close() API.
class tcp_close_completion {
public:
  explicit tcp_close_completion(tcp_connection_state *state) noexcept : state_{state} {}
  tcp_close_completion(const tcp_close_completion &) = delete;
  tcp_close_completion &operator=(const tcp_close_completion &) = delete;
  tcp_close_completion(tcp_close_completion &&) = delete;
  tcp_close_completion &operator=(tcp_close_completion &&) = delete;

  // Affinity must be checked even when close completed before this request.
  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (state_ == nullptr) {
      status_ = UV_EBADF;
      return false;
    }
    if (&continuation.promise().execution_loop() != state_->loop) {
      throw std::logic_error{"uv internal tcp close used from a different loop"};
    }
    if (state_->close_phase == tcp_close_phase::closed) {
      return false;
    }
    initiated_ = state_->request_close_and_join(continuation);
    return true;
  }

  void await_resume() { throw_if_error(status_); }

  bool initiated_close() const noexcept { return initiated_; }

private:
  tcp_connection_state *state_ = nullptr;
  int status_ = 0;
  bool initiated_ = false;
};

[[nodiscard]] tcp_close_completion close_completion(tcp_connection &) noexcept;

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

  uv_tcp_t *native_handle() noexcept { return state_ ? &state_->tcp : nullptr; }
  const uv_tcp_t *native_handle() const noexcept { return state_ ? &state_->tcp : nullptr; }
  bool closing() const noexcept { return state_ && state_->closing(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }
  bool has_active_operation() const noexcept {
    return state_ != nullptr && (state_->write_active || state_->active_read != nullptr);
  }

  class read_some_result {
  public:
    read_some_result(std::size_t count, bool eof) noexcept : count_{count}, eof_{eof} {}

    std::size_t count() const noexcept { return count_; }
    bool eof() const noexcept { return eof_; }

  private:
    std::size_t count_ = 0;
    bool eof_ = false;
  };

  class read_awaiter {
  public:
    read_awaiter(detail::tcp_connection_state *state, std::span<std::byte> buffer) noexcept
      : state_{state}, buffer_{buffer} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->closing()) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != state_->loop) {
        throw std::logic_error{"uv::tcp_connection read used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (state_->active_read != nullptr) {
        status_ = UV_EBUSY;
        return false;
      }
      if (buffer_.empty()) {
        status_ = UV_EINVAL;
        return false;
      }
      state_->active_read = this;
      continuation_ = continuation;
      status_ = uv_read_start(reinterpret_cast<uv_stream_t *>(&state_->tcp),
          &read_awaiter::on_alloc, &read_awaiter::on_read);
      if (status_ < 0) {
        state_->active_read = nullptr;
        continuation_ = {};
        return false;
      }
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &read_awaiter::on_stop_requested, this)) {
        (void)uv_read_stop(reinterpret_cast<uv_stream_t *>(&state_->tcp));
        state_->active_read = nullptr;
        continuation_ = {};
        status_ = UV_ECANCELED;
        return false;
      }
      return true;
    }

    read_some_result await_resume() {
      if (status_ == UV_EOF) {
        return {0, true};
      }
      if (status_ < 0) {
        throw_if_error(static_cast<int>(status_));
      }
      return {static_cast<std::size_t>(status_), false};
    }

  private:
    static read_awaiter &active(uv_handle_t *raw) noexcept {
      auto &state = detail::tcp_connection_state::from_handle(raw);
      return *static_cast<read_awaiter *>(state.active_read);
    }

    static void on_alloc(uv_handle_t *raw, size_t, uv_buf_t *out) noexcept {
      auto &self = active(raw);
      *out = detail::make_native_buffer_unchecked(
          reinterpret_cast<char *>(self.buffer_.data()), self.buffer_.size());
    }

    static void on_read(uv_stream_t *raw, ssize_t nread, const uv_buf_t *) noexcept {
      auto &self = active(reinterpret_cast<uv_handle_t *>(raw));
      if (nread == 0) {
        return;
      }
      self.status_ = nread;
      // libuv guarantees that uv_read_stop() prevents future read callbacks.
      // Its non-zero TTY/Windows return does not indicate a stop failure, and
      // tcp_connection is a TCP-only owner in any case.
      (void)uv_read_stop(raw);
      auto &state = *self.state_;
      state.active_read = nullptr; // Release alloc/read slots before user resumption.
      if (self.cancellation_ != nullptr) {
        self.cancellation_->unregister(self.cancellation_registration_);
      }
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<read_awaiter *>(context);
      if (self.state_ == nullptr || self.state_->active_read != &self) {
        return;
      }
      self.status_ = UV_ECANCELED;
      (void)uv_read_stop(reinterpret_cast<uv_stream_t *>(&self.state_->tcp));
      self.state_->active_read = nullptr; // Quiesce and release before resumption.
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }

    detail::tcp_connection_state *state_ = nullptr;
    std::span<std::byte> buffer_{};
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    ssize_t status_ = 0;
  };

  // Borrows buffer storage until data, EOF, or an error has stopped the read.
  [[nodiscard]] read_awaiter read_some(std::span<std::byte> buffer) noexcept {
    return read_awaiter{state_.get(), buffer};
  }

  class write_awaiter {
  public:
    write_awaiter(detail::tcp_connection_state *state, std::string_view data) noexcept
      : state_{state}, buffer_{detail::make_native_buffer_unchecked(
          const_cast<char *>(data.data()), data.size())} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->closing()) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != state_->loop) {
        throw std::logic_error{"uv::tcp_connection write used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (state_->write_active) {
        status_ = UV_EBUSY;
        return false;
      }
      state_->write_active = true;
      continuation_ = continuation;
      status_ = uv_write(&request_, reinterpret_cast<uv_stream_t *>(&state_->tcp),
          &buffer_, 1, &write_awaiter::on_write);
      if (status_ < 0) {
        state_->write_active = false;
        continuation_ = {};
        return false;
      }
      return true;
    }

    void await_resume() { throw_if_error(status_); }

  private:
    static write_awaiter &from_native(uv_write_t *raw) noexcept {
      return *reinterpret_cast<write_awaiter *>(raw);
    }

    static void on_write(uv_write_t *raw, int status) noexcept {
      auto &self = from_native(raw);
      self.status_ = status;
      self.state_->write_active = false;
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }

    uv_write_t request_{};
    detail::tcp_connection_state *state_ = nullptr;
    uv_buf_t buffer_{};
    std::coroutine_handle<> continuation_{};
    int status_ = 0;
  };

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
    assert(!state->write_active);
    assert(state->active_read == nullptr);
    if (state->write_active || state->active_read != nullptr) {
      std::terminate();
    }
    state->release_owner();
  }

  std::unique_ptr<detail::tcp_connection_state> state_{};

  friend class tcp_listener;
  friend class tcp_connection_view;
  friend detail::tcp_close_completion detail::close_completion(tcp_connection &) noexcept;
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

// Internal boundary for resource_scope. It borrows the owner; it does not
// transfer ownership or make public co_await socket.close() available.
[[nodiscard]] inline tcp_close_completion close_completion(
    tcp_connection &connection) noexcept {
  return tcp_close_completion{connection.state_.get()};
}

} // namespace detail

static_assert(std::is_standard_layout_v<tcp_connection::write_awaiter>);

} // namespace uv
