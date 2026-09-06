#pragma once

#include <chrono>
#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv {

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
      callback_.replace(std::move(cb));
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

    template<class Rep, class Period>
    void set_repeat(std::chrono::duration<Rep, Period> interval) noexcept {
      uv_timer_set_repeat(native(), millis(interval));
    }

    std::chrono::milliseconds repeat() const noexcept {
      return std::chrono::milliseconds{
        uv_timer_get_repeat(native())
      };
    }

#if UVPP_HAS_TIMER_GET_DUE_IN
    std::chrono::milliseconds due_in() const noexcept {
      return std::chrono::milliseconds{
        uv_timer_get_due_in(native())
      };
    }
#endif

  private:
    template<class Rep, class Period>
    static uint64_t millis(std::chrono::duration<Rep, Period> duration) noexcept {
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
      return ms >= 0 ? static_cast<uint64_t>(ms) : uint64_t{0};
    }

    static void timer_trampoline(uv_timer_t *raw) noexcept {
      auto &self = timer::from_native(raw);
      self.callback_.invoke([&](callback &callback) { callback(self); });
    }

    detail::persistent_callback_slot<callback> callback_{};
  };

}
