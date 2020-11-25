#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "handle.hpp"

namespace uv {

  struct FsPoll : THandle<uv_fs_poll_t, FsPoll> {
    using FsPollCb = void (*)(FsPoll *handle, int status, const uv_stat_t *prev, const uv_stat_t *curr);
    using SafeFsPollCb = void (*)(FsPoll *handle, const uv_stat_t *prev, const uv_stat_t *curr);


    bool castable(Handle &h) { return h.getType() == Handle::Type::FsPoll; }


    FsPoll(Loop *loop) {
      _safe(uv_fs_poll_init(loop, this));
    }

    void start(FsPollCb cb, const char *path, int interval) {
      _safe(uv_fs_poll_start(this, reinterpret_cast<uv_fs_poll_cb>(cb), path, interval));
    }
    template<SafeFsPollCb cb>
    void start(const char *path, int interval) {
      start([](FsPoll *handle, int status, const uv_stat_t *prev, const uv_stat_t *curr) {
          _safe(status);
          cb(handle, prev, curr);
        });
    }

    void stop() {
      _safe(uv_fs_poll_stop(this));
    }

    void getpath(char *buffer, size_t *size) {
      _safe(uv_fs_poll_getpath(this, buffer, size));
    }
  };

}
