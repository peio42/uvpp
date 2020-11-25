#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Async : THandle<uv_async_t, Async> {
    using AsyncCb = void (*)(Async *handle);

    bool castable(Handle &h) { return h.getType() == Handle::Type::Async; }


    Async(Loop *loop, AsyncCb cb) {
      _safe(uv_async_init(loop, this, reinterpret_cast<uv_async_cb>(cb)));
    }

    // template<class V = void>
    // Async(Loop *loop, AsyncCb cb, V *data = nullptr) {
    //   uv_async_init(loop, this, reinterpret_cast<uv_async_cb>(cb));
    //   this->data = static_cast<void *>(data);
    // }

    void send() {
      _safe(uv_async_send(this));
    }
  };

}
