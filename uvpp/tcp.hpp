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
      _safe(uv_tcp_init(loop, this));
    }

    Tcp(Loop *loop, unsigned int flags) {
      _safe(uv_tcp_init_ex(loop, this, flags));
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

    // void open(uv_os_sock_t sock) {
    //   uv_tcp_open(this, sock);
    // }

    void nodelay(bool enable) {
      _safe(uv_tcp_nodelay(this, enable));
    }

    void keepalive(bool enable, unsigned int delay) {
      _safe(uv_tcp_keepalive(this, enable, delay));
    }

    void simultaneous_accepts(bool enable) {
      _safe(uv_tcp_simultaneous_accepts(this, enable));
    }

    void bind(const sockaddr *addr, unsigned int flags = 0) {
      _safe(uv_tcp_bind(this, addr, flags));
    }

    template<class Z>
    void bind(const Z *addr, unsigned int flags = 0) {
      _safe(uv_tcp_bind(this, addr->sa(), flags));
    }

    void sockname(sockaddr *name, int *namelen) {
      _safe(uv_tcp_getsockname(this, name, namelen));
    }

    void peername(sockaddr *name, int *namelen) {
      _safe(uv_tcp_getpeername(this, name, namelen));
    }

    void connect(ConnectRq *req, sockaddr *addr, ConnectCb cb) {
      _safe(uv_tcp_connect(req, this, addr, reinterpret_cast<uv_connect_cb>(cb)));
    }
    // template<SafeConnectCb cb>
    // void connect(ConnectRq *req, sockaddr *addr) {
    //   connect(req, addr, _safeCb<ConnectRq, cb>);
    // }
    template<class Z>
    void connect(ConnectRq *req, Z *addr, ConnectCb cb) {
      _safe(uv_tcp_connect(req, this, addr->sa(), reinterpret_cast<uv_connect_cb>(cb)));
    }
    template<SafeConnectCb cb, class Z>
    void connect(ConnectRq *req, Z *addr) {
      connect(req, addr, _safeCb<ConnectRq, cb>);
    }

    void close_reset(CloseCb cb) {
      _safe(uv_tcp_close_reset(this, reinterpret_cast<uv_close_cb>(cb)));
    }
    template<CloseCb cb>
    void close_reset() {
      close_reset(cb);
    }
  };

}
