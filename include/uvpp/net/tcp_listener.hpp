#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/socket_address.hpp"
#include "uvpp/net/tcp_connection.hpp"

namespace uv {

namespace detail {

struct tcp_listener_state {
  uv_tcp_t tcp{};
  uv::loop *loop = nullptr;
  void *active_accept = nullptr;
  void (*deliver_accept)(void *, int) noexcept = nullptr;
  bool initialized = false;
  bool close_started = false;
  bool destroy_on_close = false;

  static tcp_listener_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_listener_state *>(bytes - offsetof(tcp_listener_state, tcp));
  }

  void close(bool destroy_when_closed) noexcept {
    if (close_started) {
      std::terminate();
    }
    close_started = true;
    destroy_on_close = destroy_when_closed;
    uv_close(reinterpret_cast<uv_handle_t *>(&tcp), &tcp_listener_state::on_close);
  }

  static void on_connection(uv_stream_t *raw, int status) noexcept {
    auto &self = from_handle(reinterpret_cast<uv_handle_t *>(raw));
    auto *accept = std::exchange(self.active_accept, nullptr);
    auto deliver = std::exchange(self.deliver_accept, nullptr);
    // uv_accept() is valid only for this notification. A long-running server
    // must therefore keep an accept waiter armed; queueing/backpressure is a
    // future scoped-server policy, not an implicit listener side effect.
    if (deliver != nullptr) {
      deliver(accept, status);
    }
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    const bool destroy = self.destroy_on_close;
    self.destroy_on_close = false;
    if (destroy) {
      delete &self;
    }
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

  uv_tcp_t *native_handle() noexcept { return state_ ? &state_->tcp : nullptr; }
  const uv_tcp_t *native_handle() const noexcept { return state_ ? &state_->tcp : nullptr; }
  bool closing() const noexcept { return state_ && state_->close_started; }

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
      if (listener_ == nullptr || listener_->close_started) {
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
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &accept_awaiter::on_stop_requested, this)) {
        listener_->active_accept = nullptr;
        listener_->deliver_accept = nullptr;
        status_ = UV_ECANCELED;
        close_untransferred_connection(std::exchange(continuation_, {}));
        return true;
      }
      return true;
    }

    tcp_connection await_resume() {
      throw_if_error(status_);
      return tcp_connection{std::move(connection_)};
    }

  private:
    void close_untransferred_connection(std::coroutine_handle<> continuation) noexcept {
      auto *connection = connection_.release();
      assert(connection != nullptr);
      connection->release_owner();
      connection->close_waiter.continuation = continuation;
      (void)connection->request_close(&connection->close_waiter);
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

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<accept_awaiter *>(context);
      if (self.listener_ == nullptr || self.listener_->active_accept != &self) {
        return;
      }
      self.listener_->active_accept = nullptr;
      self.listener_->deliver_accept = nullptr;
      self.status_ = UV_ECANCELED;
      auto continuation = std::exchange(self.continuation_, {});
      // The child handle was initialized before claiming the accept slot. Close
      // it before delivery even though no connection was transferred into it.
      self.close_untransferred_connection(continuation);
    }

    detail::tcp_listener_state *listener_ = nullptr;
    std::unique_ptr<detail::tcp_connection_state> connection_{};
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    int status_ = 0;
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
      failed->close(true);
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
    state->close(true);
  }

  std::unique_ptr<detail::tcp_listener_state> state_{};
};

} // namespace uv
