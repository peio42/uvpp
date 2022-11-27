#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "handle.hpp"

namespace uv {

  struct FsPoll : THandle<uv_fs_poll_t, FsPoll> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::FsPoll; }


    FsPoll(Loop *loop) {
      Error::safe(uv_fs_poll_init(loop, this));
    }

    using StartCb = void (*)(FsPoll *handle, int status, const uv_stat_t *prev, const uv_stat_t *curr);
    using SafeStartCb = void (*)(FsPoll *handle, const uv_stat_t *prev, const uv_stat_t *curr);
    void start(StartCb cb, const char *path, int interval) {
      Error::safe(uv_fs_poll_start(this, reinterpret_cast<uv_fs_poll_cb>(cb), path, interval));
    }
    template<SafeStartCb cb>
    void start(const char *path, int interval) {
      start([](FsPoll *handle, int status, const uv_stat_t *prev, const uv_stat_t *curr) {
          Error::safe(status);
          cb(handle, prev, curr);
        }, path, interval);
    }

    void stop() {
      Error::safe(uv_fs_poll_stop(this));
    }

    void getpath(char *buffer, size_t *size) {
      Error::safe(uv_fs_poll_getpath(this, buffer, size));
    }
  };

}
