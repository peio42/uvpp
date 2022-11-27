#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Async : THandle<uv_async_t, Async> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::Async; }


    using AsyncCb = void (*)(Async *handle);
    Async(Loop *loop, AsyncCb cb) {
      Error::safe(uv_async_init(loop, this, reinterpret_cast<uv_async_cb>(cb)));
    }
    template<AsyncCb cb>
    Async(Loop *loop) : Async(loop, cb) {}

    // template<class V = void>
    // Async(Loop *loop, AsyncCb cb, V *data = nullptr) {
    //   uv_async_init(loop, this, reinterpret_cast<uv_async_cb>(cb));
    //   this->data = static_cast<void *>(data);
    // }

    void send() {
      Error::safe(uv_async_send(this));
    }
  };

}
