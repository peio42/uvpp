#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Prepare : THandle<uv_prepare_t, Prepare> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::Prepare; }


    Prepare(Loop *loop) {
      Error::safe(uv_prepare_init(loop, this));
    }

    // template<class V = void>
    // Prepare(Loop *loop, V *data = nullptr) {
    //   uv_prepare_init(loop, this);
    //   this->data = static_cast<void *>(data);
    // }

    using StartCb = void (*)(Prepare *handle);
    void start(StartCb cb) {
      Error::safe(uv_prepare_start(this, reinterpret_cast<uv_prepare_cb>(cb)));
    }
    template<StartCb cb>
    void start() {
      start(cb);
    }

    void stop() {
      Error::safe(uv_prepare_stop(this));
    }
  };

}
