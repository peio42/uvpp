#pragma once

#include <string>
#include <string_view>

#include <uv.h>

#include "uvpp/core/error.hpp"

namespace uv {

  class ipv4 {
  public:
    ipv4(std::string_view ip, int port) {
      std::string storage{ip};
      throw_if_error(uv_ip4_addr(storage.c_str(), port, &raw_));
    }

    explicit ipv4(const sockaddr_in &addr) noexcept : raw_{addr} {}

    sockaddr *native_sockaddr() noexcept {
      return reinterpret_cast<sockaddr *>(&raw_);
    }

    const sockaddr *native_sockaddr() const noexcept {
      return reinterpret_cast<const sockaddr *>(&raw_);
    }

    sockaddr_in *native() noexcept { return &raw_; }
    const sockaddr_in *native() const noexcept { return &raw_; }

    int port() const noexcept { return ntohs(raw_.sin_port); }

    std::string to_string() const {
      char buf[16];
      throw_if_error(uv_ip4_name(&raw_, buf, sizeof(buf)));
      return std::string{buf};
    }

  private:
    sockaddr_in raw_{};
  };

  class ipv6 {
  public:
    ipv6(std::string_view ip, int port) {
      std::string storage{ip};
      throw_if_error(uv_ip6_addr(storage.c_str(), port, &raw_));
    }

    explicit ipv6(const sockaddr_in6 &addr) noexcept : raw_{addr} {}

    sockaddr *native_sockaddr() noexcept {
      return reinterpret_cast<sockaddr *>(&raw_);
    }

    const sockaddr *native_sockaddr() const noexcept {
      return reinterpret_cast<const sockaddr *>(&raw_);
    }

    sockaddr_in6 *native() noexcept { return &raw_; }
    const sockaddr_in6 *native() const noexcept { return &raw_; }

    int port() const noexcept { return ntohs(raw_.sin6_port); }

    std::string to_string() const {
      char buf[46];
      throw_if_error(uv_ip6_name(&raw_, buf, sizeof(buf)));
      return std::string{buf};
    }

  private:
    sockaddr_in6 raw_{};
  };

}

