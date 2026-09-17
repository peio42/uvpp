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
#include "uvpp/detail/accept_slot.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/detail/owner_close.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/socket_address.hpp"
#include "uvpp/net/tcp_connection.hpp"

namespace uv {

class tcp_listener;

namespace detail {

struct tcp_listener_access {
  tcp_listener *listener = nullptr;
};

struct tcp_listener_state;
using tcp_listener_close_completion = owner_close_awaiter<tcp_listener_state, false>;
using tcp_listener_close_result = owner_close_awaiter<tcp_listener_state, true>;
[[nodiscard]] tcp_listener_close_completion close_completion(tcp_listener &) noexcept;
[[nodiscard]] tcp_listener_close_result close_result(tcp_listener &) noexcept;

struct tcp_listener_state {
  uv_tcp_t tcp{};
  uv::loop *loop = nullptr;
  accept_slot accept{};
  bool initialized = false;
  async_close_state close{};

  static tcp_listener_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_listener_state *>(bytes - offsetof(tcp_listener_state, tcp));
  }

  bool closing() const noexcept { return close.closing(); }
  bool has_active_operation() const noexcept { return false; }

  // Cancelling the one-shot accept releases the listener callback slot before
  // it starts close. The awaiter closes its initialized, untransferred child
  // state before eventually resuming its task with UV_ECANCELED.
  void quiesce_active_accept() noexcept { accept.quiesce(); }

  // Starts uv_close exactly once. This is the internal close-completion path;
  // it is permitted to quiesce an armed accept before claiming close.
  bool request_close() noexcept {
    if (!close.begin()) {
      return false;
    }
    quiesce_active_accept();
    assert(!accept.claimed());
    uv_close(reinterpret_cast<uv_handle_t *>(&tcp), &tcp_listener_state::on_close);
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
    // Native close has claimed this listener.  Libuv must not deliver another
    // connection notification after uv_close(), but retaining this guard makes
    // the terminal-slot protocol robust to a stale/fault-injected callback:
    // it cannot recover a continuation from a completed accept frame.
    if (self.close.closing()) {
      return;
    }
    // uv_accept() is valid only for this notification. A long-running server
    // must therefore keep an accept waiter armed; queueing/backpressure is a
    // future scoped-server policy, not an implicit listener side effect.
    self.accept.deliver(status);
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    self.close.complete([&self]() noexcept { delete &self; });
  }
};

static_assert(std::is_standard_layout_v<tcp_listener_state>);

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

  socket_address local_address() const {
    if (!state_) {
      throw_if_error(UV_EBADF);
    }
    socket_address address;
    throw_if_error(uv_tcp_getsockname(&state_->tcp,
        address.native(), address.native_len()));
    return address;
  }

  [[nodiscard]] detail::tcp_listener_close_completion close() & noexcept {
    return detail::tcp_listener_close_completion{state_.get(), true};
  }
  detail::tcp_listener_close_completion close() && = delete;

  void request_close() {
    if (!state_) {
      throw_if_error(UV_EBADF);
    }
    (void)state_->request_close();
  }

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
      if (listener_->accept.claimed()) {
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
      listener_->accept.claim(
          this, &accept_awaiter::on_connection, &accept_awaiter::on_listener_close_requested);
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
        self.cancellation_ = nullptr;
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
        self.cancellation_ = nullptr;
      }
      auto continuation = std::exchange(self.continuation_, {});
      // The listener has already released its accept/delivery/cancellation
      // slots. Do not read listener state after this point: resource close may
      // complete before the untransferred child close resumes this task.
      self.close_untransferred_connection(continuation);
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<accept_awaiter *>(context);
      if (self.listener_ == nullptr || !self.listener_->accept.claimed_by(&self)) {
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
    assert(!state->accept.claimed());
    if (state->accept.claimed()) {
      std::terminate();
    }
    state->release_owner();
  }

  std::unique_ptr<detail::tcp_listener_state> state_{};

  friend detail::tcp_listener_close_completion detail::close_completion(
      tcp_listener &) noexcept;
  friend detail::tcp_listener_close_result detail::close_result(tcp_listener &) noexcept;
};

namespace detail {

[[nodiscard]] inline tcp_listener_close_completion close_completion(
    tcp_listener &listener) noexcept {
  return tcp_listener_close_completion{listener.state_.get(), false};
}

[[nodiscard]] inline tcp_listener_close_result close_result(
    tcp_listener &listener) noexcept {
  return tcp_listener_close_result{listener.state_.get(), true};
}

} // namespace detail

namespace ops {

[[nodiscard]] inline detail::tcp_listener_close_result close(tcp_listener &listener) noexcept {
  return detail::close_result(listener);
}

} // namespace ops

} // namespace uv
