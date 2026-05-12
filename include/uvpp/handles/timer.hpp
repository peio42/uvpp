#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uvpp {

  class timer final : public basic_handle<timer, uv_timer_t> {
  public:
    using callback = std::function<void(timer&)>;

    explicit timer(loop &l) {
      throw_if_error(uv_timer_init(l.native(), native()));
    }

    explicit timer(loop_view l) {
      throw_if_error(uv_timer_init(l.native(), native()));
    }

    template<class Rep, class Period, class RepeatRep = uint64_t, class RepeatPeriod = std::milli>
    void start(std::chrono::duration<Rep, Period> timeout,
               std::chrono::duration<RepeatRep, RepeatPeriod> repeat,
               callback cb) {
      callback_ = std::move(cb);
      throw_if_error(uv_timer_start(native(), &timer::timer_trampoline, millis(timeout), millis(repeat)));
    }

    template<class Rep, class Period>
    void start(std::chrono::duration<Rep, Period> timeout, callback cb) {
      start(timeout, std::chrono::milliseconds{0}, std::move(cb));
    }

    template<auto Callback, class Rep, class Period, class RepeatRep = uint64_t, class RepeatPeriod = std::milli>
    void start_static(std::chrono::duration<Rep, Period> timeout,
                      std::chrono::duration<RepeatRep, RepeatPeriod> repeat = std::chrono::milliseconds{0}) {
      throw_if_error(uv_timer_start(native(), [](uv_timer_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(timer::from_native(raw));
      }, millis(timeout), millis(repeat)));
    }

    void stop() {
      throw_if_error(uv_timer_stop(native()));
    }

    void again() {
      throw_if_error(uv_timer_again(native()));
    }

  private:
    template<class Rep, class Period>
    static uint64_t millis(std::chrono::duration<Rep, Period> duration) {
      return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    }

    static void timer_trampoline(uv_timer_t *raw) noexcept {
      auto &self = timer::from_native(raw);
      if (self.callback_) {
        detail::invoke_callback(self.callback_, self);
      }
    }

    callback callback_{};
  };

}
