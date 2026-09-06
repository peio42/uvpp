#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv {

  class check final : public basic_handle<check, uv_check_t> {
  public:
    using callback = std::function<void(check&)>;

    explicit check(loop &l) {
      throw_if_error(uv_check_init(l.native(), native()));
    }

    explicit check(loop_view l) {
      throw_if_error(uv_check_init(l.native(), native()));
    }

    void start(callback cb) {
      callback_.replace(std::move(cb));
      throw_if_error(uv_check_start(native(), &check::check_trampoline));
    }

    template<auto Callback>
    void start_static() {
      throw_if_error(uv_check_start(native(), [](uv_check_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(check::from_native(raw));
      }));
    }

    void stop() {
      throw_if_error(uv_check_stop(native()));
    }

  private:
    static void check_trampoline(uv_check_t *raw) noexcept {
      auto &self = check::from_native(raw);
      self.callback_.invoke([&](callback &callback) { callback(self); });
    }

    detail::persistent_callback_slot<callback> callback_{};
  };

}
