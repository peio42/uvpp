#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv {

  class async final : public basic_handle<async, uv_async_t> {
  public:
    using callback = std::function<void(async&)>;

    template<auto Callback>
    struct static_callback {};

    explicit async(loop &l) {
      init(l.native(), &async::async_trampoline);
    }

    explicit async(loop_view l) {
      init(l.native(), &async::async_trampoline);
    }

    async(loop &l, callback cb)
      : callback_{std::move(cb)} {
      init(l.native(), &async::async_trampoline);
    }

    async(loop_view l, callback cb)
      : callback_{std::move(cb)} {
      init(l.native(), &async::async_trampoline);
    }

    template<auto Callback>
    async(loop &l, static_callback<Callback>) {
      init(l.native(), [](uv_async_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(async::from_native(raw));
      });
    }

    template<auto Callback>
    async(loop_view l, static_callback<Callback>) {
      init(l.native(), [](uv_async_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(async::from_native(raw));
      });
    }

    void set_callback(callback cb) {
      callback_ = std::move(cb);
    }

    void send() {
      throw_if_error(uv_async_send(native()));
    }

  private:
    void init(uv_loop_t *loop, uv_async_cb cb) {
      throw_if_error(uv_async_init(loop, native(), cb));
    }

    static void async_trampoline(uv_async_t *raw) noexcept {
      auto &self = async::from_native(raw);
      if (self.callback_) {
        detail::invoke_callback(self.callback_, self);
      }
    }

    callback callback_{};
  };

}
