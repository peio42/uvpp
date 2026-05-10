#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uvpp {

  class prepare final : public basic_handle<prepare, uv_prepare_t> {
  public:
    using callback = std::function<void(prepare&)>;

    explicit prepare(loop &l) {
      throw_if_error(uv_prepare_init(l.native(), native()));
    }

    explicit prepare(loop_view l) {
      throw_if_error(uv_prepare_init(l.native(), native()));
    }

    void start(callback cb) {
      callback_ = std::move(cb);
      throw_if_error(uv_prepare_start(native(), &prepare::prepare_trampoline));
    }

    template<auto Callback>
    void start_static() {
      throw_if_error(uv_prepare_start(native(), [](uv_prepare_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(prepare::from_native(raw));
      }));
    }

    void stop() {
      throw_if_error(uv_prepare_stop(native()));
    }

  private:
    static void prepare_trampoline(uv_prepare_t *raw) noexcept {
      auto &self = prepare::from_native(raw);
      if (self.callback_) {
        detail::invoke_callback(self.callback_, self);
      }
    }

    callback callback_{};
  };

}
