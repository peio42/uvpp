#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/socket_address.hpp"
#include "uvpp/net/tcp_connection.hpp"

namespace uv {

class tcp_listener;

namespace detail {

enum class tcp_listener_close_phase {
  open,
  closing,
  closed,
};

struct tcp_listener_access {
  tcp_listener *listener = nullptr;
};

class tcp_listener_close_completion;
[[nodiscard]] tcp_listener_close_completion close_completion(tcp_listener &) noexcept;

struct tcp_listener_state {
  uv_tcp_t tcp{};
  uv::loop *loop = nullptr;
  void *active_accept = nullptr;
  void (*deliver_accept)(void *, int) noexcept = nullptr;
  void (*cancel_accept)(void *) noexcept = nullptr;
  bool initialized = false;
  tcp_listener_close_phase close_phase = tcp_listener_close_phase::open;
  std::vector<std::coroutine_handle<>> close_waiters{};
  std::coroutine_handle<> callback_close_waiter{};
  bool owner_released = false;
  bool close_callback_active = false;

  static tcp_listener_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_listener_state *>(bytes - offsetof(tcp_listener_state, tcp));
  }

  bool closing() const noexcept { return close_phase != tcp_listener_close_phase::open; }

  // Cancelling the one-shot accept releases the listener callback slot before
  // it starts close. The awaiter closes its initialized, untransferred child
  // state before eventually resuming its task with UV_ECANCELED.
  void quiesce_active_accept() noexcept {
    auto *accept = std::exchange(active_accept, nullptr);
    auto cancel = std::exchange(cancel_accept, nullptr);
    deliver_accept = nullptr;
    if (cancel != nullptr) {
      assert(accept != nullptr);
      cancel(accept);
    } else {
      assert(accept == nullptr);
    }
  }

  // Starts uv_close exactly once. This is the internal close-completion path;
  // it is permitted to quiesce an armed accept before claiming close.
  bool request_close() noexcept {
    if (close_phase == tcp_listener_close_phase::closed ||
        close_phase == tcp_listener_close_phase::closing) {
      return false;
    }
    quiesce_active_accept();
    assert(active_accept == nullptr);
    assert(deliver_accept == nullptr);
    assert(cancel_accept == nullptr);
    close_phase = tcp_listener_close_phase::closing;
    uv_close(reinterpret_cast<uv_handle_t *>(&tcp), &tcp_listener_state::on_close);
    return true;
  }

  bool request_close_and_join(std::coroutine_handle<> continuation) {
    // tcp_listener_close_completion checks this phase before calling us. A
    // completion observed from on_close() cannot register into that delivery.
    assert(close_phase != tcp_listener_close_phase::closed);
    if (close_phase == tcp_listener_close_phase::closed) {
      return false;
    }
    close_waiters.push_back(continuation);
    return request_close();
  }

  void release_owner() noexcept {
    assert(!owner_released);
    owner_released = true;
    if (close_phase == tcp_listener_close_phase::open) {
      (void)request_close();
      return;
    }
    if (close_phase == tcp_listener_close_phase::closed && !close_callback_active) {
      delete this;
    }
  }

  static void on_connection(uv_stream_t *raw, int status) noexcept {
    auto &self = from_handle(reinterpret_cast<uv_handle_t *>(raw));
    // Native close has claimed this listener.  Libuv must not deliver another
    // connection notification after uv_close(), but retaining this guard makes
    // the terminal-slot protocol robust to a stale/fault-injected callback:
    // it cannot recover a continuation from a completed accept frame.
    if (self.close_phase != tcp_listener_close_phase::open) {
      return;
    }
    auto *accept = std::exchange(self.active_accept, nullptr);
    auto deliver = std::exchange(self.deliver_accept, nullptr);
    (void)std::exchange(self.cancel_accept, nullptr);
    // uv_accept() is valid only for this notification. A long-running server
    // must therefore keep an accept waiter armed; queueing/backpressure is a
    // future scoped-server policy, not an implicit listener side effect.
    if (deliver != nullptr) {
      deliver(accept, status);
    }
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    self.close_phase = tcp_listener_close_phase::closed;
    self.close_callback_active = true;
    auto callback_waiter = std::exchange(self.callback_close_waiter, {});
    // Detach every frame-facing continuation before the first user resumption.
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

static_assert(std::is_standard_layout_v<tcp_listener_state>);

// Internal listener close-completion primitive. It is intentionally separate
// from the public synchronous close() decision and is used by resource_scope.
class tcp_listener_close_completion {
public:
  explicit tcp_listener_close_completion(tcp_listener_state *state) noexcept : state_{state} {}
  tcp_listener_close_completion(const tcp_listener_close_completion &) = delete;
  tcp_listener_close_completion &operator=(const tcp_listener_close_completion &) = delete;
  tcp_listener_close_completion(tcp_listener_close_completion &&) = delete;
  tcp_listener_close_completion &operator=(tcp_listener_close_completion &&) = delete;

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (state_ == nullptr) {
      status_ = UV_EBADF;
      return false;
    }
    if (&continuation.promise().execution_loop() != state_->loop) {
      throw std::logic_error{"uv internal tcp listener close used from a different loop"};
    }
    if (state_->close_phase == tcp_listener_close_phase::closed) {
      return false;
    }
    initiated_ = state_->request_close_and_join(continuation);
    return true;
  }

  void await_resume() { throw_if_error(status_); }
  bool initiated_close() const noexcept { return initiated_; }

private:
  tcp_listener_state *state_ = nullptr;
  int status_ = 0;
  bool initiated_ = false;
};

} // namespace detail

// Experimental v3 movable TCP listener. It owns stable native storage and one
// exclusive accept waiter. An accepted tcp_connection is independently owned.
class tcp_listener {
public:
  tcp_listener(const tcp_listener &) = delete;
  tcp_listener &operator=(const tcp_listener &) = delete;

  tcp_listener(tcp_listener &&other) noexcept : state_{std::move(other.state_)} {}

  tcp_listener &operator=(tcp_listener &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  tcp_listener(uv::loop &loop, const ipv4 &address, int backlog = 64) {
    initialize(loop, address.native_sockaddr(), backlog);
  }

  tcp_listener(uv::loop &loop, const ipv6 &address, int backlog = 64) {
    initialize(loop, address.native_sockaddr(), backlog);
  }

  ~tcp_listener() { reset(); }

  uv_tcp_t *native_handle() noexcept { return state_ ? &state_->tcp : nullptr; }
  const uv_tcp_t *native_handle() const noexcept { return state_ ? &state_->tcp : nullptr; }
  bool closing() const noexcept { return state_ && state_->closing(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }

  socket_address sockname() const {
    if (!state_) {
      throw_if_error(UV_EBADF);
    }
    socket_address address;
    throw_if_error(uv_tcp_getsockname(&state_->tcp,
        address.native(), address.native_len()));
    return address;
  }

  // Starts asynchronous close. The loop must continue running until its close
  // callback has released the listener's stable state.
  void close() noexcept { reset(); }

  class accept_awaiter {
  public:
    explicit accept_awaiter(detail::tcp_listener_state *listener) noexcept : listener_{listener} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (listener_ == nullptr || listener_->closing()) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != listener_->loop) {
        throw std::logic_error{"uv::tcp_listener accept used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (listener_->active_accept != nullptr) {
        status_ = UV_EBUSY;
        return false;
      }

      connection_ = std::make_unique<detail::tcp_connection_state>();
      connection_->loop = listener_->loop;
      status_ = uv_tcp_init(connection_->loop->native(), &connection_->tcp);
      if (status_ < 0) {
        return false;
      }
      connection_->initialized = true;
      continuation_ = continuation;
      listener_->active_accept = this;
      listener_->deliver_accept = &accept_awaiter::on_connection;
      listener_->cancel_accept = &accept_awaiter::on_listener_close_requested;
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &accept_awaiter::on_stop_requested, this)) {
        listener_->quiesce_active_accept();
        return true;
      }
      return true;
    }

    tcp_connection await_resume() {
      // Cancellation and accept failure own an initialized provisional child.
      // Its uv_close callback sets this flag before it can resume this frame.
      assert(!provisional_close_required_ || provisional_close_completed_);
      throw_if_error(status_);
      return tcp_connection{std::move(connection_)};
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
      }
      if (self.status_ >= 0) {
        self.status_ = uv_accept(reinterpret_cast<uv_stream_t *>(&self.listener_->tcp),
            reinterpret_cast<uv_stream_t *>(&self.connection_->tcp));
      }

      auto continuation = std::exchange(self.continuation_, {});
      if (self.status_ < 0) {
        // The accepted handle was initialized even when uv_accept failed. Keep
        // it alive through uv_close() before reporting the error to the task.
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
      }
      auto continuation = std::exchange(self.continuation_, {});
      // The listener has already released its accept/delivery/cancellation
      // slots. Do not read listener state after this point: resource close may
      // complete before the untransferred child close resumes this task.
      self.close_untransferred_connection(continuation);
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<accept_awaiter *>(context);
      if (self.listener_ == nullptr || self.listener_->active_accept != &self) {
        return;
      }
      self.listener_->quiesce_active_accept();
    }

    detail::tcp_listener_state *listener_ = nullptr;
    std::unique_ptr<detail::tcp_connection_state> connection_{};
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    int status_ = 0;
    bool provisional_close_required_ = false;
    bool provisional_close_completed_ = false;
  };

  [[nodiscard]] accept_awaiter accept() noexcept { return accept_awaiter{state_.get()}; }

private:
  void initialize(uv::loop &loop, const sockaddr *address, int backlog) {
    auto state = std::make_unique<detail::tcp_listener_state>();
    state->loop = &loop;
    int status = uv_tcp_init(loop.native(), &state->tcp);
    if (status < 0) {
      throw_if_error(status);
    }
    state->initialized = true;
    status = uv_tcp_bind(&state->tcp, address, 0);
    if (status >= 0) {
      status = uv_listen(reinterpret_cast<uv_stream_t *>(&state->tcp), backlog,
          &detail::tcp_listener_state::on_connection);
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
    assert(state->active_accept == nullptr);
    if (state->active_accept != nullptr) {
      std::terminate();
    }
    state->release_owner();
  }

  std::unique_ptr<detail::tcp_listener_state> state_{};

  friend detail::tcp_listener_close_completion detail::close_completion(
      tcp_listener &) noexcept;
};

namespace detail {

[[nodiscard]] inline tcp_listener_close_completion close_completion(
    tcp_listener &listener) noexcept {
  return tcp_listener_close_completion{listener.state_.get()};
}

} // namespace detail

} // namespace uv
