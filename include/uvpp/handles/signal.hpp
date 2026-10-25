#pragma once

#include <concepts>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/initializable_raw_watcher.hpp"

namespace uv {
  using signal_number = int;
}

namespace uv::raw {

  class signal final : public ::uv::detail::initializable_raw_watcher_handle<signal, uv_signal_t, signal_number> {
  public:
    signal() noexcept = default;

    explicit signal(loop &l) : signal(loop_view{l.native()}) {}

    explicit signal(loop_view l) {
      init(l).value();
    }

    status init(loop &l) noexcept { return init(l.view()); }

    status init(loop_view l) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      return status::from_native(uv_signal_init(l.native(), native()));
    }

    status start(signal_number signum, callback cb) {
      return start_impl(signum, std::move(cb), false);
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), signal&, signal_number>
    status start_static(signal_number signum) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      auto result = commit_start(uv_signal_start(native(), [](uv_signal_t *raw, int received) noexcept {
        ::uv::detail::invoke_static_callback<Callback>(signal::from_native(raw), received);
      }, signum));
      if (result) oneshot_ = false;
      return result;
    }

    status start_oneshot(signal_number signum, callback cb) {
      return start_impl(signum, std::move(cb), true);
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), signal&, signal_number>
    status start_oneshot_static(signal_number signum) noexcept {
      // Use a persistent native source and stop before user delivery. Native
      // one-shot dispatch can stop a subscription rearmed inside the callback.
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      auto result = commit_start(uv_signal_start(native(), [](uv_signal_t *raw, int received) noexcept {
        auto &self = signal::from_native(raw);
        if (!self.stop()) std::terminate();
        ::uv::detail::invoke_static_callback<Callback>(self, received);
      }, signum));
      if (result) oneshot_ = true;
      return result;
    }

    status stop() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return commit_stop(uv_signal_stop(native()));
    }

  private:
    status start_impl(signal_number signum, callback cb, bool oneshot) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      auto result = commit_start(uv_signal_start(native(), &signal::signal_trampoline, signum), std::move(cb));
      if (result) oneshot_ = oneshot;
      return result;
    }

    static void signal_trampoline(uv_signal_t *raw, signal_number signum) noexcept {
      auto &self = signal::from_native(raw);
      if (self.oneshot_) {
        if (uv_signal_stop(raw) < 0) std::terminate();
        self.deliver_terminal(signum);
      } else {
        self.deliver(signum);
      }
    }

    bool oneshot_ = false;
  };

}
