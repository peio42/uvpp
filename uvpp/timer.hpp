#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "handle.hpp"

namespace uv {

  struct Timer : THandle<uv_timer_t, Timer> {
    using TimerCb = void (*)(Timer *handle);

    bool castable(Handle &h) { return h.getType() == Handle::Type::Timer; }

    Timer() = default;

    Timer(Loop *loop) {
      uv_timer_init(loop, this);
    }

    // template<class V = void>
    // Timer(Loop *loop, V *data = nullptr) {
    //   _safe(uv_timer_init(loop, this));
    //   this->data = static_cast<void *>(data);
    // }

    void start(TimerCb cb, uint64_t timeout, uint64_t repeat = 0) {
      _safe(uv_timer_start(this, reinterpret_cast<uv_timer_cb>(cb), timeout, repeat));
    }
    template<TimerCb cb>
    void start(uint64_t timeout, uint64_t repeat = 0) {
      start(cb, timeout, repeat);
    }

    void stop() {
      _safe(uv_timer_stop(this));
    }

    void again() {
      _safe(uv_timer_again(this));
    }

    void set_repeat(uint64_t repeat) {
      uv_timer_set_repeat(this, repeat);
    }

    uint64_t get_repeat() {
      return uv_timer_get_repeat(this);
    }
  };

}
