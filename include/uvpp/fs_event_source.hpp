#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <uv.h>

#include "uvpp/co/cancellation.hpp"
#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/detail/owner_close.hpp"
#include "uvpp/detail/one_shot_callback_slot.hpp"
#include "uvpp/fs_event.hpp"

namespace uv {

class fs_event_source;

namespace ops {
struct fs_event_source_ops_access;
}

namespace detail {

struct fs_event_source_state;
using fs_event_source_close_completion = owner_close_awaiter<fs_event_source_state, false>;
using fs_event_source_close_result = owner_close_awaiter<fs_event_source_state, true>;
[[nodiscard]] fs_event_source_close_completion close_completion(fs_event_source &) noexcept;
[[nodiscard]] fs_event_source_close_result close_result(fs_event_source &) noexcept;

// One native subscription and one bounded, coalesced pending outcome.
struct fs_event_source_state {
  uv_fs_event_t watcher{};
  uv::loop *loop = nullptr;
  async_close_state close{};
  one_shot_callback_slot<int, fs_event, std::exception_ptr> waiter{};
  fs_event pending_event{};
  std::exception_ptr pending_exception{};
  int pending_status = 0;
  bool pending = false;

  static fs_event_source_state &from_handle(uv_handle_t *raw) noexcept {
    assert(raw->data != nullptr);
    return *static_cast<fs_event_source_state *>(raw->data);
  }

  bool closing() const noexcept { return close.closing(); }
  // close() is the terminal operation for this source: it quiesces an active
  // wait rather than rejecting it as competing I/O.
  bool has_active_operation() const noexcept { return false; }

  void cancel_waiter() noexcept { waiter.deliver(UV_ECANCELED, {}, {}); }

  void deliver_event(const char *filename, int events, int status) noexcept {
    if (close.closing()) {
      return;
    }
    // A pending failure wins over later notifications until consumed. There is
    // no lossless event history; one outcome bounds retained control state.
    if (pending && (pending_status < 0 || pending_exception)) {
      return;
    }
    fs_event event{{}, fs_event_flags::from_raw(static_cast<unsigned>(events))};
    std::exception_ptr failure;
    if (status >= 0) {
      try {
        if (filename != nullptr) {
          event.filename.emplace(filename);
        }
      } catch (...) {
        // Transport C++ materialization failures to the await expression in
        // both error policies; never unwind through the native C callback.
        failure = std::current_exception();
      }
    }
    if (waiter.claimed()) {
      waiter.deliver(status, std::move(event), std::move(failure));
      return; // Delivery may initiate close or release the owner.
    }
    if (!pending || status < 0 || failure) {
      pending_event = std::move(event);
      pending_status = status;
      pending_exception = std::move(failure);
      pending = true;
      return;
    }
    pending_event.events |= event.events;
    if (pending_event.filename != event.filename) {
      pending_event.filename.reset();
    }
  }

  bool request_close() noexcept {
    if (!close.begin()) {
      return false;
    }
    // Stop native delivery before releasing the coroutine consumer slot.  The
    // waiter may install a new action after resumption, but this source is now
    // terminal and cannot accept it.
    (void)uv_fs_event_stop(&watcher);
    cancel_waiter();
    pending = false;
    pending_event = {};
    pending_exception = {};
    pending_status = 0;
    uv_close(reinterpret_cast<uv_handle_t *>(&watcher), &fs_event_source_state::on_close);
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

  static void on_event(uv_fs_event_t *raw, const char *filename, int events, int status) noexcept {
    from_handle(reinterpret_cast<uv_handle_t *>(raw)).deliver_event(filename, events, status);
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    self.close.complete([&self]() noexcept { delete &self; });
  }
};

} // namespace detail

// Experimental v3 persistent filesystem notification source. The watched
// filesystem remains external; this owner retains only native/control storage.
class fs_event_source {
public:
  fs_event_source(const fs_event_source &) = delete;
  fs_event_source &operator=(const fs_event_source &) = delete;

  fs_event_source(fs_event_source &&other) noexcept : state_{std::move(other.state_)} {}

  fs_event_source &operator=(fs_event_source &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  fs_event_source(uv::loop &loop, std::string_view path, fs_event_options options = {}) {
    // Prepare before native initialization, so C++ setup failure needs no close.
    std::string path_storage{path};
    if (path.find('\0') != std::string_view::npos) {
      throw_if_error(UV_EINVAL);
    }
    auto state = std::make_unique<detail::fs_event_source_state>();
    state->loop = &loop;
    int status = uv_fs_event_init(loop.native(), &state->watcher);
    if (status < 0) {
      throw_if_error(status);
    }
    state->watcher.data = state.get();
    status = uv_fs_event_start(&state->watcher, &detail::fs_event_source_state::on_event,
                              path_storage.c_str(), options.raw());
    if (status < 0) {
      auto *failed = state.release();
      failed->release_owner();
      throw_if_error(status);
    }
    state_ = std::move(state);
  }

  ~fs_event_source() { reset(); }

  uv_fs_event_t *native() noexcept { return state_ ? &state_->watcher : nullptr; }
  const uv_fs_event_t *native() const noexcept { return state_ ? &state_->watcher : nullptr; }
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
    explicit next_awaiter(detail::fs_event_source_state *state) noexcept : state_{state} {}
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
        throw std::logic_error{"uv::fs_event_source next used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (state_->pending) {
        event_ = std::move(state_->pending_event);
        failure_ = std::exchange(state_->pending_exception, {});
        status_ = std::exchange(state_->pending_status, 0);
        state_->pending_event = {};
        state_->pending = false;
        return false;
      }
      if (!state_->waiter.claim(this, &next_awaiter::on_delivery)) {
        status_ = UV_EBUSY;
        return false;
      }
      continuation_ = continuation;
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &next_awaiter::on_stop_requested, this)) {
        // A pre-existing stop is normally detected above. Keep this fallback
        // synchronous: resuming from await_suspend would re-enter this frame.
        (void)state_->waiter.release(this);
        continuation_ = {};
        cancellation_ = nullptr;
        status_ = UV_ECANCELED;
        return false;
      }
      return true;
    }

    auto await_resume() {
      if (failure_) {
        std::rethrow_exception(failure_);
      }
      if constexpr (ExplicitResult) {
        if (status_ < 0) {
          return uv::result<fs_event>{uv::make_error_code(status_)};
        }
        return uv::result<fs_event>{std::move(event_)};
      } else {
        throw_if_error(status_);
        return std::move(event_);
      }
    }

  private:
    static void on_delivery(void *context, int status, fs_event delivered,
                            std::exception_ptr failure) noexcept {
      auto &self = *static_cast<next_awaiter *>(context);
      if (self.cancellation_ != nullptr) {
        self.cancellation_->unregister(self.cancellation_registration_);
        self.cancellation_ = nullptr;
      }
      auto continuation = std::exchange(self.continuation_, {});
      self.status_ = status;
      self.event_ = std::move(delivered);
      self.failure_ = std::move(failure);
      continuation.resume();
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<next_awaiter *>(context);
      if (self.state_ != nullptr) {
        self.state_->cancel_waiter();
      }
    }

    detail::fs_event_source_state *state_ = nullptr;
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    fs_event event_{};
    std::exception_ptr failure_{};
    int status_ = 0;
  };

  [[nodiscard]] next_awaiter<false> next() noexcept { return next_awaiter<false>{state_.get()}; }

  [[nodiscard]] detail::fs_event_source_close_completion close() & noexcept {
    return detail::fs_event_source_close_completion{state_.get(), false};
  }
  detail::fs_event_source_close_completion close() && = delete;

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

  std::unique_ptr<detail::fs_event_source_state> state_{};

  friend detail::fs_event_source_close_completion detail::close_completion(fs_event_source &) noexcept;
  friend detail::fs_event_source_close_result detail::close_result(fs_event_source &) noexcept;
  friend struct ops::fs_event_source_ops_access;
};

namespace detail {

[[nodiscard]] inline fs_event_source_close_completion close_completion(fs_event_source &source) noexcept {
  return fs_event_source_close_completion{source.state_.get(), false};
}

[[nodiscard]] inline fs_event_source_close_result close_result(fs_event_source &source) noexcept {
  return fs_event_source_close_result{source.state_.get(), false};
}

} // namespace detail

namespace ops {

struct fs_event_source_ops_access {
  static fs_event_source::next_awaiter<true> next(fs_event_source &source) noexcept {
    return fs_event_source::next_awaiter<true>{source.state_.get()};
  }
};

[[nodiscard]] inline fs_event_source::next_awaiter<true> next(fs_event_source &source) noexcept {
  return fs_event_source_ops_access::next(source);
}

[[nodiscard]] inline detail::fs_event_source_close_result close(fs_event_source &source) noexcept {
  return detail::close_result(source);
}

} // namespace ops

} // namespace uv
