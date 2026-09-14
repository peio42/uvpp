#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
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
#include "uvpp/detail/accept_slot.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/net/pipe_connection.hpp"

namespace uv {

class pipe_listener;

namespace detail {

struct pipe_listener_access {
  pipe_listener *listener = nullptr;
};

class pipe_listener_close_completion;
[[nodiscard]] pipe_listener_close_completion close_completion(pipe_listener &) noexcept;

struct pipe_listener_state {
  uv_pipe_t pipe{};
  uv::loop *loop = nullptr;
  accept_slot accept{};
  bool ipc = false;
  async_close_state close{};

  static pipe_listener_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<pipe_listener_state *>(
        bytes - offsetof(pipe_listener_state, pipe));
  }

  bool closing() const noexcept { return close.closing(); }

  // A listener owns the one-shot accept slot, so scope cleanup may quiesce it.
  // The awaiter closes its initialized, untransferred child before task delivery.
  void quiesce_active_accept() noexcept { accept.quiesce(); }

  bool request_close() noexcept {
    if (!close.begin()) {
      return false;
    }
    quiesce_active_accept();
    assert(!accept.claimed());
    uv_close(reinterpret_cast<uv_handle_t *>(&pipe), &pipe_listener_state::on_close);
    return true;
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

  static void on_connection(uv_stream_t *raw, int status) noexcept {
    auto &self = from_handle(reinterpret_cast<uv_handle_t *>(raw));
    // A notification without an armed awaiter is deliberately ignored by this
    // one-shot slice. It neither accepts nor queues a connection.
    if (self.close.closing()) {
      return;
    }
    self.accept.deliver(status);
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    self.close.complete([&self]() noexcept { delete &self; });
  }
};

static_assert(std::is_standard_layout_v<pipe_listener_state>);

class pipe_listener_close_completion {
public:
  explicit pipe_listener_close_completion(pipe_listener_state *state) noexcept : state_{state} {}
  pipe_listener_close_completion(const pipe_listener_close_completion &) = delete;
  pipe_listener_close_completion &operator=(const pipe_listener_close_completion &) = delete;
  pipe_listener_close_completion(pipe_listener_close_completion &&) = delete;
  pipe_listener_close_completion &operator=(pipe_listener_close_completion &&) = delete;

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (state_ == nullptr) {
      status_ = UV_EBADF;
      return false;
    }
    if (&continuation.promise().execution_loop() != state_->loop) {
      throw std::logic_error{"uv internal pipe listener close used from a different loop"};
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
  pipe_listener_state *state_ = nullptr;
  int status_ = 0;
  bool initiated_ = false;
};

} // namespace detail

// Experimental v3 local-pipe listener. It owns stable uv_pipe_t storage and
// supports one accept waiter. Each accepted pipe_connection owns independent,
// separately stable native storage.
class pipe_listener {
public:
  pipe_listener(const pipe_listener &) = delete;
  pipe_listener &operator=(const pipe_listener &) = delete;

  pipe_listener(pipe_listener &&other) noexcept : state_{std::move(other.state_)} {}

  pipe_listener &operator=(pipe_listener &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  pipe_listener(uv::loop &loop, std::string_view name, bool ipc = false, int backlog = 64) {
    initialize(loop, name, ipc, backlog);
  }

  ~pipe_listener() { reset(); }

  uv_pipe_t *native_handle() noexcept { return state_ ? &state_->pipe : nullptr; }
  const uv_pipe_t *native_handle() const noexcept { return state_ ? &state_->pipe : nullptr; }
  bool closing() const noexcept { return state_ && state_->closing(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }

  void close() noexcept { reset(); }

  class accept_awaiter {
  public:
    explicit accept_awaiter(detail::pipe_listener_state *listener) noexcept : listener_{listener} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (listener_ == nullptr || listener_->closing()) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != listener_->loop) {
        throw std::logic_error{"uv::pipe_listener accept used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (listener_->accept.claimed()) {
        status_ = UV_EBUSY;
        return false;
      }

      connection_ = std::make_unique<detail::pipe_connection_state>();
      connection_->loop = listener_->loop;
      status_ = uv_pipe_init(connection_->loop->native(), &connection_->pipe,
          listener_->ipc ? 1 : 0);
      if (status_ < 0) {
        return false;
      }
      continuation_ = continuation;
      listener_->accept.claim(
          this, &accept_awaiter::on_connection, &accept_awaiter::on_listener_close_requested);
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &accept_awaiter::on_stop_requested, this)) {
        listener_->quiesce_active_accept();
      }
      return true;
    }

    pipe_connection await_resume() {
      // Failed/cancelled accepts retain their initialized provisional pipe until
      // its close callback marks completion, before task delivery can destroy
      // this awaiter frame.
      assert(!provisional_close_required_ || provisional_close_completed_);
      throw_if_error(status_);
      return pipe_connection{std::move(connection_)};
    }

  private:
    void close_untransferred_connection(std::coroutine_handle<> continuation) noexcept {
      auto *connection = connection_.release();
      assert(connection != nullptr);
      provisional_close_required_ = true;
      connection->close_completion_destination = &provisional_close_completed_;
      connection->release_owner();
      connection->request_close_from_callback(continuation);
    }

    static void on_connection(void *opaque, int connection_status) noexcept {
      auto &self = *static_cast<accept_awaiter *>(opaque);
      self.status_ = connection_status;
      if (self.cancellation_ != nullptr) {
        self.cancellation_->unregister(self.cancellation_registration_);
        self.cancellation_ = nullptr;
      }
      if (self.status_ >= 0) {
        self.status_ = uv_accept(reinterpret_cast<uv_stream_t *>(&self.listener_->pipe),
            reinterpret_cast<uv_stream_t *>(&self.connection_->pipe));
      }

      auto continuation = std::exchange(self.continuation_, {});
      if (self.status_ < 0) {
        self.close_untransferred_connection(continuation);
        return;
      }
      continuation.resume();
    }

    static void on_listener_close_requested(void *context) noexcept {
      auto &self = *static_cast<accept_awaiter *>(context);
      self.status_ = UV_ECANCELED;
      if (self.cancellation_ != nullptr) {
        self.cancellation_->unregister(self.cancellation_registration_);
        self.cancellation_ = nullptr;
      }
      auto continuation = std::exchange(self.continuation_, {});
      // Listener close already released all listener slots. The child close
      // completion now owns the only remaining path to this task frame.
      self.close_untransferred_connection(continuation);
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<accept_awaiter *>(context);
      if (self.listener_ == nullptr || !self.listener_->accept.claimed_by(&self)) {
        return;
      }
      self.listener_->quiesce_active_accept();
    }

    detail::pipe_listener_state *listener_ = nullptr;
    std::unique_ptr<detail::pipe_connection_state> connection_{};
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    int status_ = 0;
    bool provisional_close_required_ = false;
    bool provisional_close_completed_ = false;
  };

  [[nodiscard]] accept_awaiter accept() noexcept { return accept_awaiter{state_.get()}; }

private:
  void initialize(uv::loop &loop, std::string_view name, bool ipc, int backlog) {
    auto state = std::make_unique<detail::pipe_listener_state>();
    state->loop = &loop;
    state->ipc = ipc;
    int status = uv_pipe_init(loop.native(), &state->pipe, ipc ? 1 : 0);
    if (status < 0) {
      throw_if_error(status);
    }
#if UVPP_HAS_PIPE_BIND2
    status = uv_pipe_bind2(&state->pipe, name.data(), name.size(), 0);
#else
    const std::string storage{name};
    status = uv_pipe_bind(&state->pipe, storage.c_str());
#endif
    if (status >= 0) {
      status = uv_listen(reinterpret_cast<uv_stream_t *>(&state->pipe), backlog,
          &detail::pipe_listener_state::on_connection);
    }
    if (status < 0) {
      auto *failed = state.release();
      failed->release_owner();
      throw_if_error(status);
    }
    state_ = std::move(state);
  }

  void reset() noexcept {
    if (!state_) {
      return;
    }
    auto *state = state_.release();
    // Direct close/destruction retains the experimental guard. resource_scope
    // uses close_completion(), which is allowed to quiesce one active accept.
    assert(!state->accept.claimed());
    if (state->accept.claimed()) {
      std::terminate();
    }
    state->release_owner();
  }

  std::unique_ptr<detail::pipe_listener_state> state_{};

  friend detail::pipe_listener_close_completion detail::close_completion(
      pipe_listener &) noexcept;
};

namespace detail {

[[nodiscard]] inline pipe_listener_close_completion close_completion(
    pipe_listener &listener) noexcept {
  return pipe_listener_close_completion{listener.state_.get()};
}

} // namespace detail

} // namespace uv
