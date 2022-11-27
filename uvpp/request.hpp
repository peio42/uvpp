#pragma once

#include "uv.h"
#include "error.hpp"

namespace uv {

  template<class U, class S>
  struct Request : U {
    enum class Type {
      Unknown = UV_UNKNOWN_REQ,
      Req = UV_REQ,
      Connect = UV_CONNECT,
      Write = UV_WRITE,
      Shutdown = UV_SHUTDOWN,
      Udp_Send = UV_UDP_SEND,
      Fs = UV_FS,
      Work = UV_WORK,
      Getaddrinfo = UV_GETADDRINFO,
      Getnameinfo = UV_GETNAMEINFO,
      Max = UV_REQ_TYPE_MAX
    };


    S *getHandle() { return reinterpret_cast<S *>(this->handle); }


    void cancel() {
      Error::safe(uv_cancel(this));
    }

    template<class W>
    void setData(W *data) { this->data = reinterpret_cast<void *>(data); }

    template<class W>
    W *getData() { return static_cast<W *>(this->data); }

    Type getType() {
      return this->type;
    }
  };

}
