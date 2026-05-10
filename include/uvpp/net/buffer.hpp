#pragma once

#include <cstddef>
#include <span>

#include <uv.h>

namespace uvpp {

  class buffer_view {
  public:
    buffer_view() = default;
    buffer_view(char *base, std::size_t size) noexcept
      : raw_{uv_buf_init(base, static_cast<unsigned int>(size))} {}

    uv_buf_t *native() noexcept { return &raw_; }
    const uv_buf_t *native() const noexcept { return &raw_; }

    char *data() const noexcept { return raw_.base; }
    std::size_t size() const noexcept { return raw_.len; }

    std::span<char> chars() const noexcept {
      return {raw_.base, raw_.len};
    }

    std::span<std::byte> bytes() const noexcept {
      return std::as_writable_bytes(chars());
    }

    static buffer_view from_native(const uv_buf_t &raw) noexcept {
      return buffer_view{raw.base, raw.len};
    }

  private:
    uv_buf_t raw_{};
  };

  static_assert(sizeof(buffer_view) == sizeof(uv_buf_t));
  static_assert(alignof(buffer_view) == alignof(uv_buf_t));

}
