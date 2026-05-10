#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uvpp {

  class signal final : public basic_handle<signal, uv_signal_t> {
  public:
    using callback = std::function<void(signal&, int)>;

    explicit signal(loop &l) {
      throw_if_error(uv_signal_init(l.native(), native()));
    }

    explicit signal(loop_view l) {
      throw_if_error(uv_signal_init(l.native(), native()));
    }

    void start(int signum, callback cb) {
      callback_ = std::move(cb);
      throw_if_error(uv_signal_start(native(), &signal::signal_trampoline, signum));
    }

    template<auto Callback>
    void start_static(int signum) {
      throw_if_error(uv_signal_start(native(), [](uv_signal_t *raw, int received) noexcept {
        detail::invoke_static_callback<Callback>(signal::from_native(raw), received);
      }, signum));
    }

    void start_oneshot(int signum, callback cb) {
      callback_ = std::move(cb);
      throw_if_error(uv_signal_start_oneshot(native(), &signal::signal_trampoline, signum));
    }

    template<auto Callback>
    void start_oneshot_static(int signum) {
      throw_if_error(uv_signal_start_oneshot(native(), [](uv_signal_t *raw, int received) noexcept {
        detail::invoke_static_callback<Callback>(signal::from_native(raw), received);
      }, signum));
    }

    void stop() {
      throw_if_error(uv_signal_stop(native()));
    }

  private:
    static void signal_trampoline(uv_signal_t *raw, int signum) noexcept {
      auto &self = signal::from_native(raw);
      if (self.callback_) {
        detail::invoke_callback(self.callback_, self, signum);
      }
    }

    callback callback_{};
  };

}
