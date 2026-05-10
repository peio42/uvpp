#pragma once

#include <string_view>

#include <uv.h>

#include "uvpp/core/error.hpp"

namespace uvpp {

  class ipv4 {
  public:
    ipv4(std::string_view ip, int port) {
      throw_if_error(uv_ip4_addr(ip.data(), port, &raw_));
    }

    sockaddr *native_sockaddr() noexcept {
      return reinterpret_cast<sockaddr *>(&raw_);
    }

    const sockaddr *native_sockaddr() const noexcept {
      return reinterpret_cast<const sockaddr *>(&raw_);
    }

    sockaddr_in *native() noexcept { return &raw_; }
    const sockaddr_in *native() const noexcept { return &raw_; }

  private:
    sockaddr_in raw_{};
  };

  class ipv6 {
  public:
    ipv6(std::string_view ip, int port) {
      throw_if_error(uv_ip6_addr(ip.data(), port, &raw_));
    }

    sockaddr *native_sockaddr() noexcept {
      return reinterpret_cast<sockaddr *>(&raw_);
    }

    const sockaddr *native_sockaddr() const noexcept {
      return reinterpret_cast<const sockaddr *>(&raw_);
    }

    sockaddr_in6 *native() noexcept { return &raw_; }
    const sockaddr_in6 *native() const noexcept { return &raw_; }

  private:
    sockaddr_in6 raw_{};
  };

}

