#pragma once

#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ratio>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/detail/raw_watcher.hpp"

namespace uv::raw {

  class timer final : public ::uv::detail::raw_watcher_handle<timer, uv_timer_t> {
  public:
    using duration = std::chrono::duration<std::uint64_t, std::milli>;

    explicit timer(loop &l) : timer(loop_view{l.native()}) {}

    explicit timer(loop_view l) {
      throw_if_error(uv_timer_init(l.native(), native()));
    }

    template<class Rep, class Period, class RepeatRep, class RepeatPeriod>
      requires (std::integral<Rep> || std::floating_point<Rep>) &&
               (std::integral<RepeatRep> || std::floating_point<RepeatRep>)
    status start(std::chrono::duration<Rep, Period> timeout,
                 std::chrono::duration<RepeatRep, RepeatPeriod> repeat,
                 callback cb) {
      if (closing()) return status::from_native(UV_EBADF);
      auto timeout_ms = millis(timeout);
      if (!timeout_ms) return status{timeout_ms.error()};
      auto repeat_ms = millis(repeat);
      if (!repeat_ms) return status{repeat_ms.error()};
      return commit_start(uv_timer_start(native(), &timer::timer_trampoline,
                                        timeout_ms.value(), repeat_ms.value()), std::move(cb));
    }

    template<class Rep, class Period>
      requires (std::integral<Rep> || std::floating_point<Rep>)
    status start(std::chrono::duration<Rep, Period> timeout, callback cb) {
      return start(timeout, std::chrono::milliseconds{0}, std::move(cb));
    }

    template<auto Callback, class Rep, class Period, class RepeatRep = std::uint64_t,
             class RepeatPeriod = std::milli>
      requires std::invocable<decltype(Callback), timer&> &&
               (std::integral<Rep> || std::floating_point<Rep>) &&
               (std::integral<RepeatRep> || std::floating_point<RepeatRep>)
    status start_static(std::chrono::duration<Rep, Period> timeout,
                        std::chrono::duration<RepeatRep, RepeatPeriod> repeat = std::chrono::milliseconds{0}) noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      auto timeout_ms = millis(timeout);
      if (!timeout_ms) return status{timeout_ms.error()};
      auto repeat_ms = millis(repeat);
      if (!repeat_ms) return status{repeat_ms.error()};
      return commit_start(uv_timer_start(native(), [](uv_timer_t *raw) noexcept {
        ::uv::detail::invoke_static_callback<Callback>(timer::from_native(raw));
      }, timeout_ms.value(), repeat_ms.value()));
    }

    status stop() noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      return commit_stop(uv_timer_stop(native()));
    }

    status again() noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      // Stop and one-shot completion release the subscription callable. Only
      // an active subscription can reuse it; use start to install a fresh one.
      if (!active()) return status::from_native(UV_EINVAL);
      return status::from_native(uv_timer_again(native()));
    }

    template<class Rep, class Period>
      requires (std::integral<Rep> || std::floating_point<Rep>)
    status set_repeat(std::chrono::duration<Rep, Period> interval) noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      auto value = millis(interval);
      if (!value) return status{value.error()};
      uv_timer_set_repeat(native(), value.value());
      return {};
    }

    duration repeat() const noexcept {
      return duration{uv_timer_get_repeat(native())};
    }

#if UVPP_HAS_TIMER_GET_DUE_IN
    duration due_in() const noexcept {
      return duration{uv_timer_get_due_in(native())};
    }
#endif

  private:
    template<class Rep, class Period>
    static result<std::uint64_t> millis(std::chrono::duration<Rep, Period> value) noexcept {
      using scale = std::ratio_divide<Period, std::milli>;
      if constexpr (std::integral<Rep>) {
        if (value.count() <= 0) return result<std::uint64_t>{std::uint64_t{0}};
        const auto count = static_cast<std::uint64_t>(value.count());
        if constexpr (scale::num == 1) {
          return result<std::uint64_t>{count / static_cast<std::uint64_t>(scale::den)};
        } else if constexpr (scale::den == 1) {
          if (count > (std::numeric_limits<std::uint64_t>::max)() / scale::num)
            return result<std::uint64_t>{make_error_code(UV_EINVAL)};
          return result<std::uint64_t>{count * static_cast<std::uint64_t>(scale::num)};
        }
      }
      const auto ms = std::chrono::duration<long double, std::milli>{value}.count();
      if (!std::isfinite(ms)) return result<std::uint64_t>{make_error_code(UV_EINVAL)};
      if (ms <= 0) return result<std::uint64_t>{std::uint64_t{0}};
      // 2^64 is an exact floating-point boundary even when long double is
      // 64-bit (MSVC), unlike a rounded conversion of UINT64_MAX.
      if (ms >= std::ldexp(1.0L, 64)) return result<std::uint64_t>{make_error_code(UV_EINVAL)};
      return result<std::uint64_t>{static_cast<std::uint64_t>(ms)};
    }

    static void timer_trampoline(uv_timer_t *raw) noexcept {
      auto &self = timer::from_native(raw);
      // Libuv rearms repeating timers before invoking the callback. A timer
      // which is inactive here has reached its terminal one-shot delivery.
      if (self.active()) self.deliver();
      else self.deliver_terminal();
    }
  };

}
