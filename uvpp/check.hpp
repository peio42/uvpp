#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Check : THandle<uv_check_t, Check> {
    using CheckCb = void (*)(Check *);

    bool castable(Handle &h) { return h.getType() == Handle::Type::Check; }


    Check(Loop *loop) {
      _safe(uv_check_init(loop, this));
    }

    // template<class V = void>
    // Check(Loop *loop, V *data = nullptr) {
    //   uv_check_init(loop, this);
    //   this->data = static_cast<void *>(data);
    // }

    void start(CheckCb cb) {
      _safe(uv_check_start(this, reinterpret_cast<uv_check_cb>(cb)));
    }
    template<CheckCb cb>
    void start() {
      start(cb);
    }

    void stop() {
      _safe(uv_check_stop(this));
    }
  };

}
