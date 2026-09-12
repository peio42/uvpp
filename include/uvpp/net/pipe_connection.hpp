#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/net/buffer.hpp"

namespace uv {

class pipe_connection;
class pipe_connection_view;

namespace detail {

enum class pipe_close_phase {
  open,
  closing,
  closed,
};

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
  int connect_status = 0;
  pipe_close_phase close_phase = pipe_close_phase::open;
  std::vector<std::coroutine_handle<>> close_waiters{};
  std::coroutine_handle<> callback_close_waiter{};
  bool owner_released = false;
  bool close_callback_active = false;
  bool write_active = false;
  void *active_read = nullptr;

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

  bool closing() const noexcept { return close_phase != pipe_close_phase::open; }

  bool request_close() noexcept {
    if (close_phase != pipe_close_phase::open) {
      return false;
    }
    close_phase = pipe_close_phase::closing;
    uv_close(reinterpret_cast<uv_handle_t *>(&pipe), &pipe_connection_state::on_close);
    return true;
  }

  bool request_close_and_join(std::coroutine_handle<> continuation) {
    assert(close_phase != pipe_close_phase::closed);
    if (close_phase == pipe_close_phase::closed) {
      return false;
    }
    close_waiters.push_back(continuation);
    return request_close();
  }

  // Failed connect paths originate in a libuv callback or an immediate native
  // submission result, where the fixed state-owned slot avoids allocation.
  void request_close_from_callback(std::coroutine_handle<> continuation) noexcept {
    assert(!callback_close_waiter);
    callback_close_waiter = continuation;
    (void)request_close();
  }

  void release_owner() noexcept {
    assert(!owner_released);
    owner_released = true;
    if (close_phase == pipe_close_phase::open) {
      (void)request_close();
      return;
    }
    if (close_phase == pipe_close_phase::closed && !close_callback_active) {
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
    self.close_phase = pipe_close_phase::closed;
    self.close_callback_active = true;
    auto callback_waiter = std::exchange(self.callback_close_waiter, {});
    // Detach every frame-facing continuation before the first user resumption.
    // The local vector owns only handles, so a resumed task cannot invalidate
    // callback linkage that this callback still needs to inspect.
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
    if (state_->close_phase == pipe_close_phase::closed) {
      return false;
    }
    initiated_ = state_->request_close_and_join(continuation);
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
    read_awaiter(detail::pipe_connection_state *state, std::span<std::byte> buffer) noexcept
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
        throw std::logic_error{"uv::pipe_connection read used from a different loop"};
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
      status_ = uv_read_start(reinterpret_cast<uv_stream_t *>(&state_->pipe),
          &read_awaiter::on_alloc, &read_awaiter::on_read);
      if (status_ < 0) {
        state_->active_read = nullptr;
        continuation_ = {};
        return false;
      }
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &read_awaiter::on_stop_requested, this)) {
        stop_native_or_terminate();
        release_slots();
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
      auto &state = detail::pipe_connection_state::from_handle(raw);
      assert(state.active_read != nullptr);
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
      self.stop_native_or_terminate();
      self.release_slots(); // Quiesce and release alloc/read slots before delivery.
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<read_awaiter *>(context);
      if (self.state_ == nullptr || self.state_->active_read != &self) {
        return;
      }
      self.status_ = UV_ECANCELED;
      self.stop_native_or_terminate();
      self.release_slots(); // cancellation state removes its registration first
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }

    void stop_native_or_terminate() noexcept {
      const int status = uv_read_stop(reinterpret_cast<uv_stream_t *>(&state_->pipe));
      // Once a terminal stream callback is observed, libuv must let this
      // one-shot owner quiesce before its borrowed callback slots are released.
      assert(status >= 0);
      if (status < 0) {
        std::terminate();
      }
    }

    void release_slots() noexcept {
      if (state_ != nullptr && state_->active_read == this) {
        state_->active_read = nullptr;
      }
      if (cancellation_ != nullptr) {
        cancellation_->unregister(cancellation_registration_);
        cancellation_ = nullptr;
      }
    }

    detail::pipe_connection_state *state_ = nullptr;
    std::span<std::byte> buffer_{};
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    ssize_t status_ = 0;
  };

  // Borrows buffer storage until data, EOF, error, or cancellation has stopped
  // the native read and released both callback slots.
  [[nodiscard]] read_awaiter read_some(std::span<std::byte> buffer) noexcept {
    return read_awaiter{state_.get(), buffer};
  }

  class write_awaiter {
  public:
    write_awaiter(detail::pipe_connection_state *state, std::string_view data) noexcept
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
        throw std::logic_error{"uv::pipe_connection write used from a different loop"};
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
      status_ = uv_write(&request_, reinterpret_cast<uv_stream_t *>(&state_->pipe),
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
    detail::pipe_connection_state *state_ = nullptr;
    uv_buf_t buffer_{};
    std::coroutine_handle<> continuation_{};
    int status_ = 0;
  };

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
    assert(!state->write_active);
    assert(state->active_read == nullptr);
    if (state->write_active || state->active_read != nullptr) {
      std::terminate();
    }
    state->release_owner();
  }

  std::unique_ptr<detail::pipe_connection_state> state_{};

  friend class pipe_connection_view;
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
