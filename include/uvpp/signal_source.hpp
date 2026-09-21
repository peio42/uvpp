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

#include "uvpp/co/cancellation.hpp"
#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/detail/owner_close.hpp"
#include "uvpp/handles/signal.hpp"

namespace uv {

class signal_source;

namespace ops {
struct signal_source_ops_access;
}

namespace detail {

struct signal_source_state;
using signal_source_close_completion = owner_close_awaiter<signal_source_state, false>;
using signal_source_close_result = owner_close_awaiter<signal_source_state, true>;
[[nodiscard]] signal_source_close_completion close_completion(signal_source &) noexcept;
[[nodiscard]] signal_source_close_result close_result(signal_source &) noexcept;

// One native subscription remains armed for the lifetime of its owner.  The
// source deliberately has one coroutine consumer slot; a notification received
// while that slot is free is coalesced into one pending signal.
struct signal_source_state {
  uv_signal_t signal{};
  uv::loop *loop = nullptr;
  async_close_state close{};
  void *waiter = nullptr;
  void (*deliver_waiter)(void *, signal_number) noexcept = nullptr;
  signal_number pending_signal = 0;
  bool pending = false;

  static signal_source_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<signal_source_state *>(bytes - offsetof(signal_source_state, signal));
  }

  bool closing() const noexcept { return close.closing(); }
  // close() is the terminal operation for this source: it quiesces an active
  // wait rather than rejecting it as competing I/O.
  bool has_active_operation() const noexcept { return false; }

  bool claim_waiter(void *context, void (*deliver)(void *, signal_number) noexcept) noexcept {
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

  void cancel_waiter() noexcept {
    auto *context = std::exchange(waiter, nullptr);
    auto deliver = std::exchange(deliver_waiter, nullptr);
    if (deliver != nullptr) {
      deliver(context, UV_ECANCELED);
    }
  }

  void deliver_signal(signal_number signum) noexcept {
    if (close.closing()) {
      return;
    }
    auto *context = std::exchange(waiter, nullptr);
    auto deliver = std::exchange(deliver_waiter, nullptr);
    if (deliver != nullptr) {
      deliver(context, signum);
      return;
    }
    pending_signal = signum;
    pending = true;
  }

  bool request_close() noexcept {
    if (!close.begin()) {
      return false;
    }
    // Stop native delivery before releasing the coroutine consumer slot.  The
    // waiter may install a new action after resumption, but this source is now
    // terminal and cannot accept it.
    (void)uv_signal_stop(&signal);
    cancel_waiter();
    uv_close(reinterpret_cast<uv_handle_t *>(&signal), &signal_source_state::on_close);
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

  static void on_signal(uv_signal_t *raw, int signum) noexcept {
    from_handle(reinterpret_cast<uv_handle_t *>(raw)).deliver_signal(signum);
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    self.close.complete([&self]() noexcept { delete &self; });
  }
};

static_assert(std::is_standard_layout_v<signal_source_state>);

} // namespace detail

// Experimental v3 persistent signal subscription. Construction starts watching
// signum and the source remains active between next() calls. It has one active
// coroutine consumer; notifications with no consumer are coalesced into one
// pending event.
class signal_source {
public:
  signal_source(const signal_source &) = delete;
  signal_source &operator=(const signal_source &) = delete;

  signal_source(signal_source &&other) noexcept : state_{std::move(other.state_)} {}

  signal_source &operator=(signal_source &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  signal_source(uv::loop &loop, signal_number signum) {
    auto state = std::make_unique<detail::signal_source_state>();
    state->loop = &loop;
    int status = uv_signal_init(loop.native(), &state->signal);
    if (status < 0) {
      throw_if_error(status);
    }
    status = uv_signal_start(&state->signal, &detail::signal_source_state::on_signal, signum);
    if (status < 0) {
      auto *failed = state.release();
      failed->release_owner();
      throw_if_error(status);
    }
    state_ = std::move(state);
  }

  ~signal_source() { reset(); }

  uv_signal_t *native() noexcept { return state_ ? &state_->signal : nullptr; }
  const uv_signal_t *native() const noexcept { return state_ ? &state_->signal : nullptr; }
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
    explicit next_awaiter(detail::signal_source_state *state) noexcept : state_{state} {}
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
        throw std::logic_error{"uv::signal_source next used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (state_->pending) {
        event_ = state_->pending_signal;
        state_->pending = false;
        return false;
      }
      if (!state_->claim_waiter(this, &next_awaiter::on_delivery)) {
        status_ = UV_EBUSY;
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
      return true;
    }

    auto await_resume() {
      if constexpr (ExplicitResult) {
        if (status_ < 0) {
          return uv::result<signal_number>{uv::make_error_code(status_)};
        }
        return uv::result<signal_number>{event_};
      } else {
        throw_if_error(status_);
        return event_;
      }
    }

  private:
    static void on_delivery(void *context, signal_number delivered) noexcept {
      auto &self = *static_cast<next_awaiter *>(context);
      if (self.cancellation_ != nullptr) {
        self.cancellation_->unregister(self.cancellation_registration_);
        self.cancellation_ = nullptr;
      }
      auto continuation = std::exchange(self.continuation_, {});
      if (delivered == UV_ECANCELED) {
        self.status_ = UV_ECANCELED;
      } else {
        self.event_ = delivered;
      }
      continuation.resume();
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<next_awaiter *>(context);
      if (self.state_ != nullptr) {
        self.state_->cancel_waiter();
      }
    }

    detail::signal_source_state *state_ = nullptr;
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    signal_number event_ = 0;
    int status_ = 0;
  };

  [[nodiscard]] next_awaiter<false> next() noexcept { return next_awaiter<false>{state_.get()}; }

  [[nodiscard]] detail::signal_source_close_completion close() & noexcept {
    return detail::signal_source_close_completion{state_.get(), false};
  }
  detail::signal_source_close_completion close() && = delete;

  void request_close() {
    if (!state_) {
      throw_if_error(UV_EBADF);
    }
    (void)state_->request_close();
  }

private:
  void reset() noexcept {
    if (!state_) {
      return;
    }
    auto *state = state_.release();
    state->release_owner();
  }

  std::unique_ptr<detail::signal_source_state> state_{};

  friend detail::signal_source_close_completion detail::close_completion(signal_source &) noexcept;
  friend detail::signal_source_close_result detail::close_result(signal_source &) noexcept;
  friend struct ops::signal_source_ops_access;
};

namespace detail {

[[nodiscard]] inline signal_source_close_completion close_completion(signal_source &source) noexcept {
  return signal_source_close_completion{source.state_.get(), false};
}

[[nodiscard]] inline signal_source_close_result close_result(signal_source &source) noexcept {
  return signal_source_close_result{source.state_.get(), false};
}

} // namespace detail

namespace ops {

struct signal_source_ops_access {
  static signal_source::next_awaiter<true> next(signal_source &source) noexcept {
    return signal_source::next_awaiter<true>{source.state_.get()};
  }
};

[[nodiscard]] inline signal_source::next_awaiter<true> next(signal_source &source) noexcept {
  return signal_source_ops_access::next(source);
}

[[nodiscard]] inline detail::signal_source_close_result close(signal_source &source) noexcept {
  return detail::close_result(source);
}

} // namespace ops

} // namespace uv
