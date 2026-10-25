#pragma once

#include <concepts>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/initializable_raw_watcher.hpp"

namespace uv::raw {

  class prepare final : public ::uv::detail::initializable_raw_watcher_handle<prepare, uv_prepare_t> {
  public:
    prepare() noexcept = default;

    explicit prepare(loop &l) : prepare(loop_view{l.native()}) {}

    explicit prepare(loop_view l) {
      init(l).value();
    }

    status init(loop &l) noexcept { return init(l.view()); }

    status init(loop_view l) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      return status::from_native(uv_prepare_init(l.native(), native()));
    }

    status start(callback cb) {
      return restart(&prepare::prepare_trampoline, std::move(cb));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), prepare&>
    status start_static() noexcept {
      return restart([](uv_prepare_t *raw) noexcept {
        ::uv::detail::invoke_static_callback<Callback>(prepare::from_native(raw));
      });
    }

    status stop() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return commit_stop(uv_prepare_stop(native()));
    }

  private:
    status restart(uv_prepare_cb native_callback, callback cb = {}) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      // Native start on an active loop watcher does not replace its callback.
      const auto stopped = uv_prepare_stop(native());
      if (stopped < 0) return status::from_native(stopped);
      return commit_start(uv_prepare_start(native(), native_callback), std::move(cb));
    }

    static void prepare_trampoline(uv_prepare_t *raw) noexcept {
      prepare::from_native(raw).deliver();
    }
  };

}
