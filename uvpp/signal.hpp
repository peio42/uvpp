#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Signal : THandle<uv_signal_t, Signal> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::Signal; }


    Signal(Loop *loop) {
      Error::safe(uv_signal_init(loop, this));
    }

    // template<class V = void>
    // Signal(Loop *loop, V *data = nullptr) {
    //   uv_signal_init(loop, this);
    //   this->data = static_cast<void *>(data);
    // }

    using StartCb = void (*)(Signal *handle, int signum);
    void start(StartCb cb, int signum) {
      Error::safe(uv_signal_start(this, reinterpret_cast<uv_signal_cb>(cb), signum));
    }
    template<StartCb cb>
    void start(int signum) {
      start(cb, signum);
    }

    void start_oneshot(StartCb cb, int signum) {
      Error::safe(uv_signal_start_oneshot(this, reinterpret_cast<uv_signal_cb>(cb), signum));
    }

    void stop() {
      Error::safe(uv_signal_stop(this));
    }
  };

}
