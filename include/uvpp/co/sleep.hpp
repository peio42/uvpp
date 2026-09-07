#pragma once

#include <chrono>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <type_traits>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"

namespace uv::co {

namespace detail {

class sleep_awaiter final {
public:
  explicit sleep_awaiter(uint64_t timeout) noexcept : timeout_{timeout} {}

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    continuation_ = continuation;

    status_ = uv_timer_init(continuation.promise().execution_loop().native(), &timer_);
    if (status_ < 0) {
      return false;
    }
    status_ = uv_timer_start(&timer_, &sleep_awaiter::timer_trampoline, timeout_, 0);
    if (status_ < 0) {
      uv_close(reinterpret_cast<uv_handle_t *>(&timer_), &sleep_awaiter::close_trampoline);
    }
    return true;
  }

  void await_resume() {
    throw_if_error(status_);
  }

private:
  static sleep_awaiter &from_native(uv_timer_t *timer) noexcept {
    // timer_ is the first member. This standard-layout operation object stays
    // inside the suspended coroutine frame until uv_close completes.
    return *reinterpret_cast<sleep_awaiter *>(timer);
  }

  static void timer_trampoline(uv_timer_t *timer) noexcept {
    (void)uv_timer_stop(timer);
    uv_close(reinterpret_cast<uv_handle_t *>(timer), &sleep_awaiter::close_trampoline);
  }

  static void close_trampoline(uv_handle_t *handle) noexcept {
    auto &self = from_native(reinterpret_cast<uv_timer_t *>(handle));
    self.continuation_.resume();
  }

  uv_timer_t timer_{};
  std::coroutine_handle<> continuation_{};
  uint64_t timeout_ = 0;
  int status_ = 0;
};

static_assert(std::is_standard_layout_v<sleep_awaiter>);

template<class Rep, class Period>
uint64_t timer_timeout(std::chrono::duration<Rep, Period> duration) noexcept {
  const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(duration).count();
  return milliseconds > 0 ? static_cast<uint64_t>(milliseconds) : 0;
}

} // namespace detail

// Experimental v3 one-shot timer awaitable. It uses the loop inherited from the
// spawned task and rounds positive sub-millisecond durations up to one millisecond.
template<class Rep, class Period>
[[nodiscard]] detail::sleep_awaiter sleep_for(std::chrono::duration<Rep, Period> duration) noexcept {
  return detail::sleep_awaiter{detail::timer_timeout(duration)};
}

} // namespace uv::co
