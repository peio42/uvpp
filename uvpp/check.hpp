#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Check : THandle<uv_check_t, Check> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::Check; }


    Check(Loop *loop) {
      Error::safe(uv_check_init(loop, this));
    }

    // template<class V = void>
    // Check(Loop *loop, V *data = nullptr) {
    //   uv_check_init(loop, this);
    //   this->data = static_cast<void *>(data);
    // }

    using StartCb = void (*)(Check *);
    void start(StartCb cb) {
      Error::safe(uv_check_start(this, reinterpret_cast<uv_check_cb>(cb)));
    }
    template<StartCb cb>
    void start() {
      start(cb);
    }

    void stop() {
      Error::safe(uv_check_stop(this));
    }
  };

}
