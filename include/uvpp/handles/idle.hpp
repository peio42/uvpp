#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uvpp {

  class idle final : public basic_handle<idle, uv_idle_t> {
  public:
    using callback = std::function<void(idle&)>;

    explicit idle(loop &l) {
      throw_if_error(uv_idle_init(l.native(), native()));
    }

    explicit idle(loop_view l) {
      throw_if_error(uv_idle_init(l.native(), native()));
    }

    void start(callback cb) {
      callback_ = std::move(cb);
      throw_if_error(uv_idle_start(native(), &idle::idle_trampoline));
    }

    template<auto Callback>
    void start_static() {
      throw_if_error(uv_idle_start(native(), [](uv_idle_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(idle::from_native(raw));
      }));
    }

    void stop() {
      throw_if_error(uv_idle_stop(native()));
    }

  private:
    static void idle_trampoline(uv_idle_t *raw) noexcept {
      auto &self = idle::from_native(raw);
      if (self.callback_) {
        detail::invoke_callback(self.callback_, self);
      }
    }

    callback callback_{};
  };

}
