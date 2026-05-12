#pragma once

#include <cassert>

#include <uv.h>

#include "uvpp/net/address.hpp"

namespace uv {

  class socket_address {
  public:
    socket_address() : len_{static_cast<int>(sizeof(storage_))} {}

    bool is_v4() const noexcept { return storage_.ss_family == AF_INET; }
    bool is_v6() const noexcept { return storage_.ss_family == AF_INET6; }

    ipv4 to_v4() const {
      assert(is_v4());
      return ipv4{as<sockaddr_in>()};
    }

    ipv6 to_v6() const {
      assert(is_v6());
      return ipv6{as<sockaddr_in6>()};
    }

    int port() const noexcept {
      if (is_v4()) return ntohs(as<sockaddr_in>().sin_port);
      if (is_v6()) return ntohs(as<sockaddr_in6>().sin6_port);
      return 0;
    }

    sockaddr *native() noexcept { return reinterpret_cast<sockaddr *>(&storage_); }
    const sockaddr *native() const noexcept { return reinterpret_cast<const sockaddr *>(&storage_); }
    int *native_len() noexcept { return &len_; }

  private:
    template<class T>
    const T &as() const noexcept { return reinterpret_cast<const T &>(storage_); }

    sockaddr_storage storage_{};
    int len_;
  };

}
