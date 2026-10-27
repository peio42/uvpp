#pragma once

#include <concepts>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/initializable_raw_watcher.hpp"

namespace uv::raw {

  class idle final : public ::uv::detail::initializable_raw_watcher_handle<idle, uv_idle_t> {
  public:
    idle() noexcept = default;

    explicit idle(loop &l) : idle(loop_view{l.native()}) {}

    explicit idle(loop_view l) {
      init(l).throw_if_error();
    }

    status init(loop &l) noexcept { return init(l.view()); }

    status init(loop_view l) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      return status::from_native(uv_idle_init(l.native(), native()));
    }

    status start(callback cb) {
      return restart(&idle::idle_trampoline, std::move(cb));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), idle&>
    status start_static() noexcept {
      return restart([](uv_idle_t *raw) noexcept {
        ::uv::detail::invoke_static_callback<Callback>(idle::from_native(raw));
      });
    }

    status stop() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return commit_stop(uv_idle_stop(native()));
    }

  private:
    status restart(uv_idle_cb native_callback, callback cb = {}) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      // Native start on an active loop watcher does not replace its callback.
      const auto stopped = uv_idle_stop(native());
      if (stopped < 0) return status::from_native(stopped);
      return commit_start(uv_idle_start(native(), native_callback), std::move(cb));
    }

    static void idle_trampoline(uv_idle_t *raw) noexcept {
      idle::from_native(raw).deliver();
    }
  };

}
