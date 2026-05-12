#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <uv.h>

namespace uv {

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

  class owned_buffer {
  public:
    owned_buffer() = default;

    explicit owned_buffer(std::size_t size)
      : storage_(size) {}

    explicit owned_buffer(std::span<const std::byte> bytes)
      : storage_(bytes.begin(), bytes.end()) {}

    std::byte *data() noexcept { return storage_.data(); }
    const std::byte *data() const noexcept { return storage_.data(); }

    std::size_t size() const noexcept { return storage_.size(); }
    bool empty() const noexcept { return storage_.empty(); }

    void resize(std::size_t size) { storage_.resize(size); }

    std::span<std::byte> bytes() noexcept {
      return storage_;
    }

    std::span<const std::byte> bytes() const noexcept {
      return storage_;
    }

    std::span<char> chars() noexcept {
      return {reinterpret_cast<char *>(storage_.data()), storage_.size()};
    }

    buffer_view view() noexcept {
      return {reinterpret_cast<char *>(storage_.data()), storage_.size()};
    }

  private:
    std::vector<std::byte> storage_;
  };

}
