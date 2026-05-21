#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv {

  using signal_number = int;

  class signal final : public basic_handle<signal, uv_signal_t> {
  public:
    using callback = std::function<void(signal&, signal_number)>;

    explicit signal(loop &l) {
      throw_if_error(uv_signal_init(l.native(), native()));
    }

    explicit signal(loop_view l) {
      throw_if_error(uv_signal_init(l.native(), native()));
    }

    void start(signal_number signum, callback cb) {
      callback_ = std::move(cb);
      throw_if_error(uv_signal_start(native(), &signal::signal_trampoline, signum));
    }

    template<auto Callback>
    void start_static(signal_number signum) {
      throw_if_error(uv_signal_start(native(), [](uv_signal_t *raw, int received) noexcept {
        detail::invoke_static_callback<Callback>(signal::from_native(raw), received);
      }, signum));
    }

    void start_oneshot(signal_number signum, callback cb) {
      callback_ = std::move(cb);
      throw_if_error(uv_signal_start_oneshot(native(), &signal::signal_trampoline, signum));
    }

    template<auto Callback>
    void start_oneshot_static(signal_number signum) {
      throw_if_error(uv_signal_start_oneshot(native(), [](uv_signal_t *raw, int received) noexcept {
        detail::invoke_static_callback<Callback>(signal::from_native(raw), received);
      }, signum));
    }

    void stop() {
      throw_if_error(uv_signal_stop(native()));
    }

  private:
    static void signal_trampoline(uv_signal_t *raw, signal_number signum) noexcept {
      auto &self = signal::from_native(raw);
      if (self.callback_) {
        detail::invoke_callback(self.callback_, self, signum);
      }
    }

    callback callback_{};
  };

}
