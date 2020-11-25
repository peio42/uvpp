#pragma once

#include "uv.h"

namespace uv {

  struct Error {
    int err;

    Error(int _err) : err{_err} { }

    const char *name() const {
      return uv_err_name(err);
    }

    char *name(char *buf, size_t buflen) const {
      return uv_err_name_r(err, buf, buflen);
    }

    const char *message() const {
      return uv_strerror(err);
    }

    char *message(char *buf, size_t buflen) const {
      return uv_strerror_r(err, buf, buflen);
    }
  };

  inline int _safe(int res) {
    if (res < 0)
      throw Error(res);

    return res;
  }

  template<class T, void (*cb)(T *)>
  inline void _safeCb(T *prm, int status) {
    _safe(status);
    cb(prm);
  }

  template<class T, void (*cb)(T *)>
  inline void _safeReqCb(T *req) {
    _safe(req->result);
    cb(req);
  }
}
