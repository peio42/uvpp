#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

#include <uv.h>

#include "uvpp/co/cancellation.hpp"
#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/detail/owner_close.hpp"
#include "uvpp/handles/poll.hpp"

namespace uv {

class poll_source;

namespace ops {
struct poll_source_ops_access;
}

namespace detail {

struct poll_source_state;
using poll_source_close_completion = owner_close_awaiter<poll_source_state, false>;
using poll_source_close_result = owner_close_awaiter<poll_source_state, true>;
[[nodiscard]] poll_source_close_completion close_completion(poll_source &) noexcept;
[[nodiscard]] poll_source_close_result close_result(poll_source &) noexcept;

// Native polling is armed only for an active next(). Readiness is not queued:
// each wait observes the descriptor again, using libuv's level triggering.
struct poll_source_state {
  uv_poll_t poll{};
  uv::loop *loop = nullptr;
  async_close_state close{};
  void *waiter = nullptr;
  void (*deliver_waiter)(void *, int, poll_events) noexcept = nullptr;

  static poll_source_state &from_handle(uv_handle_t *raw) noexcept {
    assert(raw->data != nullptr);
    return *static_cast<poll_source_state *>(raw->data);
  }

  bool closing() const noexcept { return close.closing(); }
  // close() is the terminal operation for this source: it quiesces an active
  // wait rather than rejecting it as competing I/O.
  bool has_active_operation() const noexcept { return false; }

  bool claim_waiter(void *context, void (*deliver)(void *, int, poll_events) noexcept) noexcept {
    if (waiter != nullptr) {
      return false;
    }
    waiter = context;
    deliver_waiter = deliver;
    return true;
  }

  bool release_waiter(void *context) noexcept {
    if (waiter != context) {
      return false;
    }
    waiter = nullptr;
    deliver_waiter = nullptr;
    return true;
  }

  void deliver_event(int status, poll_events events) noexcept {
    // Quiesce and release the slot before user code can rearm, move, or destroy
    // the owner. Do not access this state after delivering the continuation.
    (void)uv_poll_stop(&poll);
    auto *context = std::exchange(waiter, nullptr);
    auto deliver = std::exchange(deliver_waiter, nullptr);
    if (deliver != nullptr) {
      deliver(context, status, events);
    }
  }

  void cancel_waiter() noexcept { deliver_event(UV_ECANCELED, {}); }

  bool request_close() noexcept {
    if (!close.begin()) {
      return false;
    }
    // Stop native delivery before releasing the coroutine consumer slot.  The
    // waiter may install a new action after resumption, but this source is now
    // terminal and cannot accept it.
    cancel_waiter();
    uv_close(reinterpret_cast<uv_handle_t *>(&poll), &poll_source_state::on_close);
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

  static void on_poll(uv_poll_t *raw, int status, int events) noexcept {
    from_handle(reinterpret_cast<uv_handle_t *>(raw)).deliver_event(
        status, poll_events::from_raw(events));
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    self.close.complete([&self]() noexcept { delete &self; });
  }
};

} // namespace detail

// Experimental v3 borrowed-descriptor readiness source. The source owns only
// stable uv_poll_t storage; each next(mask) arms one native wait and stops it
// before delivery. The descriptor is never closed by this owner.
class poll_source {
public:
  poll_source(const poll_source &) = delete;
  poll_source &operator=(const poll_source &) = delete;

  poll_source(poll_source &&other) noexcept : state_{std::move(other.state_)} {}

  poll_source &operator=(poll_source &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  poll_source(uv::loop &loop, int fd) {
    initialize(loop, [fd](uv_loop_t *native_loop, uv_poll_t *poll) {
      return uv_poll_init(native_loop, poll, fd);
    });
  }

  poll_source(uv::loop &loop, uv_os_sock_t socket, socket_poll_t) {
    initialize(loop, [socket](uv_loop_t *native_loop, uv_poll_t *poll) {
      return uv_poll_init_socket(native_loop, poll, socket);
    });
  }

  ~poll_source() { reset(); }

  uv_poll_t *native() noexcept { return state_ ? &state_->poll : nullptr; }
  const uv_poll_t *native() const noexcept { return state_ ? &state_->poll : nullptr; }
  uv_handle_t *native_handle() noexcept { return reinterpret_cast<uv_handle_t *>(native()); }
  const uv_handle_t *native_handle() const noexcept {
    return reinterpret_cast<const uv_handle_t *>(native());
  }
  bool closing() const noexcept { return state_ && state_->closing(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }

  template<bool ExplicitResult>
  class next_awaiter {
  public:
    explicit next_awaiter(detail::poll_source_state *state, poll_events events) noexcept
      : state_{state}, requested_{events} {}
    next_awaiter(const next_awaiter &) = delete;
    next_awaiter &operator=(const next_awaiter &) = delete;
    next_awaiter(next_awaiter &&) = delete;
    next_awaiter &operator=(next_awaiter &&) = delete;

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->closing()) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != state_->loop) {
        throw std::logic_error{"uv::poll_source next used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (!state_->claim_waiter(this, &next_awaiter::on_delivery)) {
        status_ = UV_EBUSY;
        return false;
      }
      constexpr int allowed = UV_READABLE | UV_WRITABLE
#if UVPP_HAS_POLL_DISCONNECT
          | UV_DISCONNECT
#endif
#if UVPP_HAS_POLL_PRIORITIZED
          | UV_PRIORITIZED
#endif
          ;
      if (!requested_ || (requested_.raw() & ~allowed) != 0) {
        (void)state_->release_waiter(this);
        status_ = UV_EINVAL;
        return false;
      }
      continuation_ = continuation;
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &next_awaiter::on_stop_requested, this)) {
        // A pre-existing stop is normally detected above. Keep this fallback
        // synchronous: resuming from await_suspend would re-enter this frame.
        (void)state_->release_waiter(this);
        continuation_ = {};
        cancellation_ = nullptr;
        status_ = UV_ECANCELED;
        return false;
      }
      status_ = uv_poll_start(&state_->poll, requested_.raw(), &detail::poll_source_state::on_poll);
      if (status_ < 0) {
        if (cancellation_ != nullptr) {
          cancellation_->unregister(cancellation_registration_);
          cancellation_ = nullptr;
        }
        (void)state_->release_waiter(this);
        continuation_ = {};
        return false;
      }
      return true;
    }

    auto await_resume() {
      if constexpr (ExplicitResult) {
        if (status_ < 0) {
          return uv::result<poll_events>{uv::make_error_code(status_)};
        }
        return uv::result<poll_events>{event_};
      } else {
        throw_if_error(status_);
        return event_;
      }
    }

  private:
    static void on_delivery(void *context, int status, poll_events delivered) noexcept {
      auto &self = *static_cast<next_awaiter *>(context);
      if (self.cancellation_ != nullptr) {
        self.cancellation_->unregister(self.cancellation_registration_);
        self.cancellation_ = nullptr;
      }
      auto continuation = std::exchange(self.continuation_, {});
      self.status_ = status;
      self.event_ = delivered;
      continuation.resume();
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<next_awaiter *>(context);
      if (self.state_ != nullptr) {
        self.state_->cancel_waiter();
      }
    }

    detail::poll_source_state *state_ = nullptr;
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    poll_events requested_{};
    poll_events event_{};
    int status_ = 0;
  };

  [[nodiscard]] next_awaiter<false> next(poll_events events) noexcept {
    return next_awaiter<false>{state_.get(), events};
  }

  [[nodiscard]] detail::poll_source_close_completion close() & noexcept {
    return detail::poll_source_close_completion{state_.get(), false};
  }
  detail::poll_source_close_completion close() && = delete;

  void request_close() {
    if (!state_) {
      throw_if_error(UV_EBADF);
    }
    (void)state_->request_close();
  }

private:
  template<class Initialize>
  void initialize(uv::loop &loop, Initialize init) {
    auto state = std::make_unique<detail::poll_source_state>();
    state->loop = &loop;
    throw_if_error(init(loop.native(), &state->poll));
    state->poll.data = state.get();
    state_ = std::move(state);
  }

  void reset() noexcept {
    if (!state_) {
      return;
    }
    auto *state = state_.release();
    state->release_owner();
  }

  std::unique_ptr<detail::poll_source_state> state_{};

  friend detail::poll_source_close_completion detail::close_completion(poll_source &) noexcept;
  friend detail::poll_source_close_result detail::close_result(poll_source &) noexcept;
  friend struct ops::poll_source_ops_access;
};

namespace detail {

[[nodiscard]] inline poll_source_close_completion close_completion(poll_source &source) noexcept {
  return poll_source_close_completion{source.state_.get(), false};
}

[[nodiscard]] inline poll_source_close_result close_result(poll_source &source) noexcept {
  return poll_source_close_result{source.state_.get(), false};
}

} // namespace detail

namespace ops {

struct poll_source_ops_access {
  static poll_source::next_awaiter<true> next(poll_source &source, poll_events events) noexcept {
    return poll_source::next_awaiter<true>{source.state_.get(), events};
  }
};

[[nodiscard]] inline poll_source::next_awaiter<true> next(poll_source &source, poll_events events) noexcept {
  return poll_source_ops_access::next(source, events);
}

[[nodiscard]] inline detail::poll_source_close_result close(poll_source &source) noexcept {
  return detail::close_result(source);
}

} // namespace ops

} // namespace uv
