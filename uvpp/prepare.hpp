#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Prepare : THandle<uv_prepare_t, Prepare> {
    using PrepareCb = void (*)(Prepare *handle);

    bool castable(Handle &h) { return h.getType() == Handle::Type::Prepare; }


    Prepare(Loop *loop) {
      _safe(uv_prepare_init(loop, this));
    }

    // template<class V = void>
    // Prepare(Loop *loop, V *data = nullptr) {
    //   uv_prepare_init(loop, this);
    //   this->data = static_cast<void *>(data);
    // }

    void start(PrepareCb cb) {
      _safe(uv_prepare_start(this, reinterpret_cast<uv_prepare_cb>(cb)));
    }
    template<PrepareCb cb>
    void start() {
      start(cb);
    }

    void stop() {
      _safe(uv_prepare_stop(this));
    }
  };

}
