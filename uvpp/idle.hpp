#pragma once

#include "uv.h"
#include "error.h"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Idle : THandle<uv_idle_t, Idle> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::Idle; }


    Idle(Loop *loop) {
      Error::safe(uv_idle_init(loop, this));
    }

    // template<class V = void>
    // Idle(Loop *loop, V *data = nullptr) {
    //   uv_idle_init(loop, this);
    //   this->data = static_cast<void *>(data);
    // }

    using StartCb = void (*)(Idle *handle);
    void start(StartCb cb) {
      Error::safe(uv_idle_start(this, reinterpret_cast<uv_idle_cb>(cb)));
    }
    template<StartCb cb>
    void start() {
      start(cb);
    }

    void stop() {
      Error::safe(uv_idle_stop(this));
    }
  };

}
