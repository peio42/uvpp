#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/detail/stream_io.hpp"
#include "uvpp/net/buffer.hpp"

namespace uv {

class pipe_connection;
class pipe_connection_view;
class pipe_listener;

namespace detail {

// This access token is lifetime bookkeeping for a borrowed high-level view; it
// does not own the pipe resource. resource_scope clears connection before it
// destroys its sole pipe_connection owner.
struct pipe_connection_access {
  pipe_connection *connection = nullptr;
};

[[nodiscard]] pipe_connection_view make_pipe_connection_view(
    std::shared_ptr<pipe_connection_access>) noexcept;

struct pipe_connection_state {
  uv_connect_t connect{};
  uv_pipe_t pipe{};
  uv::loop *loop = nullptr;
  std::coroutine_handle<> connect_continuation{};
  std::unique_ptr<pipe_connection_state> *provisional_owner = nullptr;
  int *connect_status_destination = nullptr;
  bool *close_completion_destination = nullptr;
  int connect_status = 0;
  async_close_state close{};
  stream_io_state io{};

  static pipe_connection_state &from_connect(uv_connect_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<pipe_connection_state *>(
        bytes - offsetof(pipe_connection_state, connect));
  }

  static pipe_connection_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<pipe_connection_state *>(
        bytes - offsetof(pipe_connection_state, pipe));
  }

  bool closing() const noexcept { return close.closing(); }

  uv_stream_t *stream_handle() noexcept {
    return reinterpret_cast<uv_stream_t *>(&pipe);
  }

  uv::loop &execution_loop() noexcept {
    assert(loop != nullptr);
    return *loop;
  }

  stream_io_state &io_state() noexcept { return io; }

  bool request_close() noexcept {
    if (!close.begin()) {
      return false;
    }
    uv_close(reinterpret_cast<uv_handle_t *>(&pipe), &pipe_connection_state::on_close);
    return true;
  }

  // Failed connect paths originate in a libuv callback or an immediate native
  // submission result, where the fixed state-owned slot avoids allocation.
  void request_close_from_callback(std::coroutine_handle<> continuation) noexcept {
    close.set_callback_waiter(continuation);
    (void)request_close();
  }

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

static_assert(std::is_standard_layout_v<pipe_connection_state>);

class pipe_close_completion {
public:
  explicit pipe_close_completion(pipe_connection_state *state) noexcept : state_{state} {}
  pipe_close_completion(const pipe_close_completion &) = delete;
  pipe_close_completion &operator=(const pipe_close_completion &) = delete;
  pipe_close_completion(pipe_close_completion &&) = delete;
  pipe_close_completion &operator=(pipe_close_completion &&) = delete;

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (state_ == nullptr) {
      status_ = UV_EBADF;
      return false;
    }
    if (&continuation.promise().execution_loop() != state_->loop) {
      throw std::logic_error{"uv internal pipe close used from a different loop"};
    }
    if (state_->close.closed()) {
      return false;
    }
    if (!state_->close.add_waiter(continuation)) {
      return false;
    }
    initiated_ = state_->request_close();
    return true;
  }

  void await_resume() { throw_if_error(status_); }
  bool initiated_close() const noexcept { return initiated_; }

private:
  pipe_connection_state *state_ = nullptr;
  int status_ = 0;
  bool initiated_ = false;
};

[[nodiscard]] pipe_close_completion close_completion(pipe_connection &) noexcept;

} // namespace detail

// Experimental v3 movable pipe client owner. Its single allocation keeps the
// uv_pipe_t address stable; a move transfers only that allocation. Destruction
// initiates internal close and storage survives until the native callback.
class pipe_connection {
public:
  pipe_connection(const pipe_connection &) = delete;
  pipe_connection &operator=(const pipe_connection &) = delete;

  pipe_connection(pipe_connection &&other) noexcept : state_{std::move(other.state_)} {}

  pipe_connection &operator=(pipe_connection &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  ~pipe_connection() { reset(); }

  uv_pipe_t *native_handle() noexcept { return state_ ? &state_->pipe : nullptr; }
  const uv_pipe_t *native_handle() const noexcept { return state_ ? &state_->pipe : nullptr; }
  bool closing() const noexcept { return state_ && state_->closing(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }
  bool has_active_operation() const noexcept {
    return state_ != nullptr && (state_->io.write_active || state_->io.active_read != nullptr);
  }

  using read_some_result = detail::stream_read_some_result;
  using read_awaiter = detail::stream_read_awaiter<detail::pipe_connection_state>;

  // Borrows buffer storage until data, EOF, error, or cancellation has stopped
  // the native read and released both callback slots.
  [[nodiscard]] read_awaiter read_some(std::span<std::byte> buffer) noexcept {
    return read_awaiter{state_.get(), buffer};
  }

  using write_awaiter = detail::stream_write_awaiter<detail::pipe_connection_state>;

  // Borrows data until the native write completion invokes the awaiting task.
  [[nodiscard]] write_awaiter write(std::string_view data) {
    detail::check_buffer_length(data.size());
    return write_awaiter{state_.get(), data};
  }

  class connect_awaiter {
  public:
    connect_awaiter(std::string_view name, bool ipc) : name_{name}, ipc_{ipc} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }

      state_ = std::make_unique<detail::pipe_connection_state>();
      auto &state = *state_;
      state.loop = &continuation.promise().execution_loop();
      state.connect_continuation = continuation;
      state.provisional_owner = &state_;
      state.connect_status_destination = &status_;
      status_ = uv_pipe_init(state.loop->native(), &state.pipe, ipc_ ? 1 : 0);
      state.connect_status = status_;
      if (status_ < 0) {
        return false;
      }

#if UVPP_HAS_PIPE_CONNECT2
      status_ = uv_pipe_connect2(&state.connect, &state.pipe, name_.data(), name_.size(), 0,
          &detail::pipe_connection_state::on_connect);
      state.connect_status = status_;
      if (status_ < 0) {
        auto resume = std::exchange(state.connect_continuation, {});
        auto *owned = state_.release();
        assert(owned == &state);
        state.release_owner();
        state.request_close_from_callback(resume);
      }
#else
      // The legacy API reports all connect failures asynchronously. name_ is
      // awaiter-owned, so its NUL-terminated storage remains valid through
      // initiation without borrowing the caller's string_view.
      uv_pipe_connect(&state.connect, &state.pipe, name_.c_str(),
          &detail::pipe_connection_state::on_connect);
#endif
      return true;
    }

    pipe_connection await_resume() {
      throw_if_error(status_);
      state_->provisional_owner = nullptr;
      state_->connect_status_destination = nullptr;
      return pipe_connection{std::move(state_)};
    }

  private:
    std::string name_{};
    bool ipc_ = false;
    std::unique_ptr<detail::pipe_connection_state> state_{};
    int status_ = 0;
  };

  // Connects to a local pipe name. The name is copied during awaiter creation;
  // `ipc` only selects libuv's handle-passing mode and does not expose an IPC
  // transfer API in this initial coroutine slice.
  [[nodiscard]] static connect_awaiter connect(std::string_view name, bool ipc = false) {
    return connect_awaiter{name, ipc};
  }

private:
  explicit pipe_connection(std::unique_ptr<detail::pipe_connection_state> state) noexcept
    : state_{std::move(state)} {}

  void reset() noexcept {
    if (!state_) {
      return;
    }
    auto *state = state_.release();
    assert(!state->io.write_active);
    assert(state->io.active_read == nullptr);
    if (state->io.write_active || state->io.active_read != nullptr) {
      std::terminate();
    }
    state->release_owner();
  }

  std::unique_ptr<detail::pipe_connection_state> state_{};

  friend class pipe_connection_view;
  friend class pipe_listener;
  friend detail::pipe_close_completion detail::close_completion(pipe_connection &) noexcept;
};

// A non-owning pipe facade produced by resource_scope. It retains only an
// access token and diagnoses use after resource cleanup before native access.
class pipe_connection_view {
public:
  [[nodiscard]] pipe_connection::read_awaiter read_some(std::span<std::byte> buffer) const {
    return connection().read_some(buffer);
  }

  [[nodiscard]] pipe_connection::write_awaiter write(std::string_view data) const {
    return connection().write(data);
  }

private:
  explicit pipe_connection_view(std::shared_ptr<detail::pipe_connection_access> access) noexcept
    : access_{std::move(access)} {}

  pipe_connection &connection() const {
    if (!access_ || access_->connection == nullptr) {
      throw std::logic_error{"uv::pipe_connection_view used after resource cleanup"};
    }
    return *access_->connection;
  }

  std::shared_ptr<detail::pipe_connection_access> access_{};

  friend pipe_connection_view detail::make_pipe_connection_view(
      std::shared_ptr<detail::pipe_connection_access>) noexcept;
};

namespace detail {

[[nodiscard]] inline pipe_connection_view make_pipe_connection_view(
    std::shared_ptr<pipe_connection_access> access) noexcept {
  return pipe_connection_view{std::move(access)};
}

// Internal boundary for resource_scope; no public co_await close() decision is
// made by this experimental owner.
[[nodiscard]] inline pipe_close_completion close_completion(
    pipe_connection &connection) noexcept {
  return pipe_close_completion{connection.state_.get()};
}

} // namespace detail

static_assert(std::is_standard_layout_v<pipe_connection::write_awaiter>);

} // namespace uv
