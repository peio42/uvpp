#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "handle.hpp"

namespace uv {

  struct SockAddr;

  struct Udp : THandle<uv_udp_t, Udp> {
    enum class Flags : unsigned int {
      None = 0,
      IPv6_Only = UV_UDP_IPV6ONLY,
      Partial = UV_UDP_PARTIAL,
      ReuseAddr = UV_UDP_REUSEADDR,
#  ifdef UV_UDP_MMSG_CHUNK
      MMSG_Chunk = UV_UDP_MMSG_CHUNK,
#  endif
#  ifdef UV_UDP_MMSG_FREE
      MMSG_Free = UV_UDP_MMSG_FREE,
#  endif
#  ifdef UV_UDP_LINUX_RECVERR
      Linux_RecvErr = UV_UDP_LINUX_RECVERR,
#  endif
#  ifdef UV_UDP_RECVMMSG
      RecvMMSG = UV_UDP_RECVMMSG,
#  endif
    };

    bool castable(Handle &h) { return h.getType() == Handle::Type::Udp; }


    Udp() = default;

    Udp(Loop *loop) {
      Error::safe(uv_udp_init(loop, this));
    }

    Udp(Loop *loop, unsigned flags) {
      Error::safe(uv_udp_init_ex(loop, this, flags));
    }

    void init(Loop *loop) {
      Error::safe(uv_udp_init(loop, this));
    }

    // void open(uv_os_sock_t sock) {
    //   uv_udp_open(this, sock);
    // }

    // void bind(const struct sockaddr *addr, unsigned int flags = 0) {
    //   Error::safe(uv_udp_bind(this, addr, flags));
    // }

    void bind(const auto *addr, Flags flags = Flags::None) {
      Error::safe(uv_udp_bind(this, addr->sa(), static_cast<unsigned int>(flags)));
    }

    void connect(const auto *addr) {
      Error::safe(uv_udp_connect(this, addr->sa()));
    }

    void get_peername(auto *name, int *namelen) {
      Error::safe(uv_udp_getpeername(this, name->sa(), namelen));
    }

    void get_sockname(auto *name, int *namelen) {
      Error::safe(uv_udp_getsockname(this, name->sa(), namelen));
    }

    void set_membership(const char *multicast_addr , const char *interface_addr, uv_membership membership) {
      Error::safe(uv_udp_set_membership(this, multicast_addr, interface_addr, membership));
    }

    void set_source_membership(const char* multicast_addr, const char* interface_addr, const char* source_addr, uv_membership membership) {
      Error::safe(uv_udp_set_source_membership(this, multicast_addr, interface_addr, source_addr, membership));
    }

    void set_multicast_loop(bool on) {
      Error::safe(uv_udp_set_multicast_loop(this, on));
    }

    void set_multicast_ttl(int ttl) {
      Error::safe(uv_udp_set_multicast_ttl(this, ttl));
    }

    void set_multicast_interface(const char *interface_addr) {
      Error::safe(uv_udp_set_multicast_interface(this, interface_addr));
    }

    void set_broadcast(bool on) {
      Error::safe(uv_udp_set_broadcast(this, on));
    }

    void set_ttl(int ttl) {
      Error::safe(uv_udp_set_ttl(this, ttl));
    }

    // void send(SendRq *req, const Buffer bufs[], unsigned int nbufs, const sockaddr *addr, SendCb cb) {
    //   Error::safe(uv_udp_send(req, this, reinterpret_cast<const uv_buf_t *>(bufs), nbufs, addr, reinterpret_cast<uv_udp_send_cb>(cb)));
    // }

    using SendRq = Request<uv_udp_send_t, Udp>;
    using SendCb = void (*)(SendRq *req, int status);
    using SafeSendCb = void (*)(SendRq *req);
    void send(SendRq *req, const Buffer bufs[], unsigned int nbufs, auto *addr, SendCb cb) {
      Error::safe(uv_udp_send(req, this, reinterpret_cast<const uv_buf_t *>(bufs), nbufs, addr->sa(), reinterpret_cast<uv_udp_send_cb>(cb)));
    }
    template<SafeSendCb cb>
    void send(SendRq *req, const Buffer bufs[], unsigned int nbufs, auto *addr) {
      send(req, bufs, nbufs, addr, Error::safeCb<SendRq, cb>);
    }

    // void try_send(const Buffer bufs[], unsigned int nbufs, const sockaddr *addr) {
    //   Error::safe(uv_udp_try_send(this, reinterpret_cast<const uv_buf_t *>(bufs), nbufs, addr));
    // }

    void trySend(const Buffer bufs[], unsigned int nbufs, auto *addr) {
      Error::safe(uv_udp_try_send(this, reinterpret_cast<const uv_buf_t *>(bufs), nbufs, addr->sa()));
    }

    using RecvCb = void (*)(Udp *handle, ssize_t nread, const Buffer *buf, const SockAddr *addr, unsigned int flags);
    void recvStart(AllocCb alloc_cb, RecvCb recv_cb) {
      Error::safe(uv_udp_recv_start(this, reinterpret_cast<uv_alloc_cb>(alloc_cb), reinterpret_cast<uv_udp_recv_cb>(recv_cb)));
    }
    template<AllocCb alloc_cb, RecvCb recv_cb>
    void recvStart() {
      recvStart(alloc_cb, [](Udp *handle, ssize_t nread, const Buffer *buf, const SockAddr *addr, unsigned int flags) {
          Error::safe(nread);
          recv_cb(handle, nread, buf, addr, flags);
        });
    }


    bool using_recvmmsg() {
      return uv_udp_using_recvmmsg(this);
    }

    void recvStop() {
      Error::safe(uv_udp_recv_stop(this));
    }
  };

  inline Udp::Flags operator | (Udp::Flags lhs, Udp::Flags rhs) {
    return static_cast<Udp::Flags>(static_cast<uv_udp_flags>(lhs) | static_cast<uv_udp_flags>(rhs));
  }
  inline Udp::Flags operator & (Udp::Flags lhs, Udp::Flags rhs) {
    return static_cast<Udp::Flags>(static_cast<uv_udp_flags>(lhs) & static_cast<uv_udp_flags>(rhs));
  }
  inline bool operator % (Udp::Flags lhs, Udp::Flags rhs) {
    return (static_cast<uv_udp_flags>(lhs) & static_cast<uv_udp_flags>(rhs)) != 0;
  }
}
