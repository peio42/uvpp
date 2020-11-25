#pragma once

#include "uv.h"
#include "error.h"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Idle : THandle<uv_idle_t, Idle> {
    using IdleCb = void (*)(Idle *handle);

    bool castable(Handle &h) { return h.getType() == Handle::Type::Idle; }


    Idle(Loop *loop) {
      _safe(uv_idle_init(loop, this));
    }

    // template<class V = void>
    // Idle(Loop *loop, V *data = nullptr) {
    //   uv_idle_init(loop, this);
    //   this->data = static_cast<void *>(data);
    // }

    void start(IdleCb cb) {
      _safe(uv_idle_start(this, reinterpret_cast<uv_idle_cb>(cb)));
    }
    template<IdleCb cb>
    void start() {
      start(cb);
    }

    void stop() {
      _safe(uv_idle_stop(this));
    }
  };

}
