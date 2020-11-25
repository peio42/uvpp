#pragma once

#include "uv.h"

namespace uv {

  struct Buffer : uv_buf_t {
    Buffer() = default;

    Buffer(char *_base, unsigned int _len) {
      // uv_buf_init(base, len);
      base = _base;
      len = _len;
    }

    void init(char *_base, unsigned int _len) {
      // uv_buf_init(base, len);
      base = _base;
      len = _len;
    }

    void allocate(unsigned int size) {
      base = new char [size];
      len = size;
    }

    void cleanup() const {
      if (base)
        delete base;
    }
  };


  struct SockAddr;

  template<class T>
  struct TSockAddr : T {
    const SockAddr *sa() const { return reinterpret_cast<const SockAddr *>(this); }
  };

  struct IPv4 : TSockAddr<sockaddr_in> {
    IPv4(const char *ip, int port) { _safe(uv_ip4_addr(ip, port, this)); }
    void name(char *dst, size_t size) const { _safe(uv_ip4_name(this, dst, size)); }
  };

  struct IPv6 : TSockAddr<sockaddr_in6> {
    IPv6(const char *ip, int port) { _safe(uv_ip6_addr(ip, port, this)); }
    void name(char *dst, size_t size) const { _safe(uv_ip6_name(this, dst, size)); }
  };

  struct SockAddr : TSockAddr<sockaddr> {
    void name(char *dst, size_t size) const {
      switch(sa_family) {
      case AF_INET:
        _safe(uv_ip4_name(reinterpret_cast<const IPv4 *>(this), dst, size));
        break;
      case AF_INET6:
        _safe(uv_ip6_name(reinterpret_cast<const IPv6 *>(this), dst, size));
        break;
      default:
        throw UV_EAFNOSUPPORT; //uv::Error:: ??
      }
    }
  };

  using AnyAddress = TSockAddr<sockaddr_storage>;


  constexpr auto hrtime = uv_hrtime;
}
