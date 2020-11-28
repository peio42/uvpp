#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "handle.hpp"

namespace uv {

  struct SockAddr;

  struct Udp : THandle<uv_udp_t, Udp> {
    using SendRq = Request<uv_udp_send_t, Udp>;

    using SendCb = void (*)(SendRq *req, int status);
    using RecvCb = void (*)(Udp *handle, ssize_t nread, const Buffer *buf, const SockAddr *addr, unsigned int flags);

    using SafeSendCb = void (*)(SendRq *req);

    enum class Flags : unsigned int {
      None = 0,
      IPv6_Only = UV_UDP_IPV6ONLY,
      Partial = UV_UDP_PARTIAL,
      ReuseAddr = UV_UDP_REUSEADDR,
#  ifdef UV_UDP_MMSG_CHUNK
      MMSG_Chunk = UV_UDP_MMSG_CHUNK,
#  endif
#  ifdef UV_UDP_RECVMMSG
      RecvMMSG = UV_UDP_RECVMMSG,
#  endif
    };

    bool castable(Handle &h) { return h.getType() == Handle::Type::Udp; }


    Udp() = default;

    Udp(Loop *loop) {
      _safe(uv_udp_init(loop, this));
    }

    Udp(Loop *loop, unsigned flags) {
      _safe(uv_udp_init_ex(loop, this, flags));
    }

    void init(Loop *loop) {
      _safe(uv_udp_init(loop, this));
    }

    // void open(uv_os_sock_t sock) {
    //   uv_udp_open(this, sock);
    // }

    // void bind(const struct sockaddr *addr, unsigned int flags = 0) {
    //   _safe(uv_udp_bind(this, addr, flags));
    // }

    template<class Z>
    void bind(const Z *addr, Flags flags = Flags::None) {
      _safe(uv_udp_bind(this, addr->sa(), static_cast<unsigned int>(flags)));
    }

    void connect(const struct sockaddr *addr) {
      _safe(uv_udp_connect(this, addr));
    }

    void get_peername(struct sockaddr *name, int *namelen) {
      _safe(uv_udp_getpeername(this, name, namelen));
    }

    void get_sockname(struct sockaddr *name, int *namelen) {
      _safe(uv_udp_getsockname(this, name, namelen));
    }

    void set_membership(const char *multicast_addr , const char *interface_addr, uv_membership membership) {
      _safe(uv_udp_set_membership(this, multicast_addr, interface_addr, membership));
    }

    void set_source_membership(const char* multicast_addr, const char* interface_addr, const char* source_addr, uv_membership membership) {
      _safe(uv_udp_set_source_membership(this, multicast_addr, interface_addr, source_addr, membership));
    }

    void set_multicast_loop(bool on) {
      _safe(uv_udp_set_multicast_loop(this, on));
    }

    void set_multicast_ttl(int ttl) {
      _safe(uv_udp_set_multicast_ttl(this, ttl));
    }

    void set_multicast_interface(const char *interface_addr) {
      _safe(uv_udp_set_multicast_interface(this, interface_addr));
    }

    void set_broadcast(bool on) {
      _safe(uv_udp_set_broadcast(this, on));
    }

    void set_ttl(int ttl) {
      _safe(uv_udp_set_ttl(this, ttl));
    }

    // void send(SendRq *req, const Buffer bufs[], unsigned int nbufs, const sockaddr *addr, SendCb cb) {
    //   _safe(uv_udp_send(req, this, reinterpret_cast<const uv_buf_t *>(bufs), nbufs, addr, reinterpret_cast<uv_udp_send_cb>(cb)));
    // }

    template<class Z>
    void send(SendRq *req, const Buffer bufs[], unsigned int nbufs, const Z *addr, SendCb cb) {
      _safe(uv_udp_send(req, this, reinterpret_cast<const uv_buf_t *>(bufs), nbufs, addr->sa(), reinterpret_cast<uv_udp_send_cb>(cb)));
    }
    template<SafeSendCb cb, class Z>
    void send(SendRq *req, const Buffer bufs[], unsigned int nbufs, const Z *addr) {
      send(req, bufs, nbufs, addr, _safeCb<SendRq, cb>);
    }

    // void try_send(const Buffer bufs[], unsigned int nbufs, const sockaddr *addr) {
    //   _safe(uv_udp_try_send(this, reinterpret_cast<const uv_buf_t *>(bufs), nbufs, addr));
    // }

    template<class Z>
    void try_send(const Buffer bufs[], unsigned int nbufs, const Z *addr) {
      _safe(uv_udp_try_send(this, reinterpret_cast<const uv_buf_t *>(bufs), nbufs, addr->sa()));
    }

    void recv_start(AllocCb alloc_cb, RecvCb recv_cb) {
      _safe(uv_udp_recv_start(this, reinterpret_cast<uv_alloc_cb>(alloc_cb), reinterpret_cast<uv_udp_recv_cb>(recv_cb)));
    }
    template<AllocCb alloc_cb, RecvCb recv_cb>
    void recv_start() {
      recv_start(alloc_cb, [](Udp *handle, ssize_t nread, const Buffer *buf, const SockAddr *addr, unsigned int flags) {
          _safe(nread);
          recv_cb(handle, nread, buf, addr, flags);
        });
    }


#  ifdef uv_udp_using_recvmmsg
    bool using_recvmmsg() {
      return uv_udp_using_recvmmsg(this);
    }
#  endif

    void recv_stop() {
      _safe(uv_udp_recv_stop(this));
    }
  };

  inline Udp::Flags operator | (Udp::Flags lhs, Udp::Flags rhs) {
    return static_cast<Udp::Flags>(static_cast<uv_udp_flags>(lhs) | static_cast<uv_udp_flags>(rhs));
  }
  inline Udp::Flags operator & (Udp::Flags lhs, Udp::Flags rhs) {
    return static_cast<Udp::Flags>(static_cast<uv_udp_flags>(lhs) & static_cast<uv_udp_flags>(rhs));
  }
  inline bool operator %(Udp::Flags lhs, Udp::Flags rhs) {
    return (static_cast<uv_udp_flags>(lhs) & static_cast<uv_udp_flags>(rhs)) != 0;
  }
}
