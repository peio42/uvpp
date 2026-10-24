#pragma once

#include <concepts>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/raw_watcher.hpp"

namespace uv::raw {

  class check final : public ::uv::detail::raw_watcher_handle<check, uv_check_t> {
  public:
    explicit check(loop &l) : check(loop_view{l.native()}) {}

    explicit check(loop_view l) {
      throw_if_error(uv_check_init(l.native(), native()));
    }

    status start(callback cb) {
      return restart(&check::check_trampoline, std::move(cb));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), check&>
    status start_static() noexcept {
      return restart([](uv_check_t *raw) noexcept {
        ::uv::detail::invoke_static_callback<Callback>(check::from_native(raw));
      });
    }

    status stop() noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      return commit_stop(uv_check_stop(native()));
    }

  private:
    status restart(uv_check_cb native_callback, callback cb = {}) noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      // Native start on an active loop watcher does not replace its callback.
      const auto stopped = uv_check_stop(native());
      if (stopped < 0) return status::from_native(stopped);
      return commit_start(uv_check_start(native(), native_callback), std::move(cb));
    }

    static void check_trampoline(uv_check_t *raw) noexcept {
      check::from_native(raw).deliver();
    }
  };

}
