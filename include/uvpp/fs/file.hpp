#pragma once

#include <uv.h>

namespace uvpp {

  class file_descriptor {
  public:
    constexpr file_descriptor() noexcept = default;

    explicit constexpr file_descriptor(uv_file value) noexcept
      : value_{value} {}

    constexpr uv_file native() const noexcept {
      return value_;
    }

    constexpr explicit operator bool() const noexcept {
      return value_ >= 0;
    }

  private:
    uv_file value_ = -1;
  };

  constexpr bool operator==(file_descriptor lhs, file_descriptor rhs) noexcept {
    return lhs.native() == rhs.native();
  }

  constexpr bool operator!=(file_descriptor lhs, file_descriptor rhs) noexcept {
    return !(lhs == rhs);
  }

}
