#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "stream.hpp"

namespace uv {

  struct Tcp : TStream<uv_tcp_t, Tcp> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::Tcp; }


    Tcp(Loop *loop) {
      Error::safe(uv_tcp_init(loop, this));
    }

    Tcp(Loop *loop, unsigned int flags) {
      Error::safe(uv_tcp_init_ex(loop, this, flags));
    }


    // template<class V = void>
    // Tcp(Loop *loop, V *data = nullptr) {
    //   uv_tcp_init(loop, this);
    //   this->data = static_cast<void *>(data);
    // }

    // template<class V = void>
    // Tcp(Loop *loop, unsigned int flags, V *data = nullptr) {
    //   uv_tcp_init_ex(loop, this, flags);
    //   this->data = static_cast<void *>(data);
    // }

    void open(uv_os_sock_t sock) {
      uv_tcp_open(this, sock);
    }

    void nodelay(bool enable) {
      Error::safe(uv_tcp_nodelay(this, enable));
    }

    void keepalive(bool enable, unsigned int delay = 0) {
      Error::safe(uv_tcp_keepalive(this, enable, delay));
    }

    void simultaneous_accepts(bool enable) {
      Error::safe(uv_tcp_simultaneous_accepts(this, enable));
    }

    void bind(const auto *addr, unsigned int flags = 0) {
      Error::safe(uv_tcp_bind(this, addr->sa(), flags));
    }

    void sockname(auto *name, int *namelen) {
      Error::safe(uv_tcp_getsockname(this, name->sa(), namelen));
    }

    void peername(auto *name, int *namelen) {
      Error::safe(uv_tcp_getpeername(this, name->sa(), namelen));
    }

    void connect(ConnectRq *req, auto *addr, ConnectCb cb) {
      Error::safe(uv_tcp_connect(req, this, addr->sa(), reinterpret_cast<uv_connect_cb>(cb)));
    }
    template<SafeConnectCb cb>
    void connect(ConnectRq *req, auto *addr) {
      connect(req, addr, Error::safeCb<ConnectRq, cb>);
    }

    void close_reset(CloseCb cb) {
      Error::safe(uv_tcp_close_reset(this, reinterpret_cast<uv_close_cb>(cb)));
    }
    template<CloseCb cb>
    void close_reset() {
      close_reset(cb);
    }

    static void socketpair(int type, int protocol, uv_os_sock_t socket_vector[2], int flags0, int flags1) {
      Error::safe(uv_socketpair(type, protocol, socket_vector, flags0, flags1));
    }
  };

}
