#pragma once

#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#include <uv.h>

namespace uv {

  namespace detail {

    using native_buffer_length = decltype(uv_buf_t::len);

    static_assert(std::numeric_limits<native_buffer_length>::digits <= std::numeric_limits<std::size_t>::digits);

    constexpr bool buffer_length_fits(std::size_t length) noexcept {
      return length <= static_cast<std::size_t>(std::numeric_limits<native_buffer_length>::max());
    }

    inline void check_buffer_length(std::size_t length) {
      if (!buffer_length_fits(length)) {
        throw std::length_error{"uv::buffer_view length exceeds the native buffer limit"};
      }
    }

    constexpr uv_buf_t make_native_buffer_unchecked(char *base, std::size_t length) noexcept {
      uv_buf_t raw{};
      raw.base = base;
      raw.len = static_cast<native_buffer_length>(length);
      return raw;
    }

    inline uv_buf_t make_native_buffer(char *base, std::size_t length) {
      check_buffer_length(length);
      return make_native_buffer_unchecked(base, length);
    }

    constexpr bool buffer_count_fits(std::size_t count) noexcept {
      return count <= static_cast<std::size_t>(std::numeric_limits<unsigned int>::max());
    }

    constexpr unsigned int narrow_buffer_count_unchecked(std::size_t count) noexcept {
      return static_cast<unsigned int>(count);
    }

    inline unsigned int checked_buffer_count(std::size_t count) {
      if (!buffer_count_fits(count)) {
        throw std::length_error{"number of buffers exceeds the libuv limit"};
      }
      return narrow_buffer_count_unchecked(count);
    }

  }

  class buffer_view {
  public:
    buffer_view() = default;
    buffer_view(char *base, std::size_t size)
      : raw_{detail::make_native_buffer(base, size)} {}

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
      buffer_view view;
      view.raw_ = raw;
      return view;
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

    buffer_view view() {
      return {reinterpret_cast<char *>(storage_.data()), storage_.size()};
    }

  private:
    std::vector<std::byte> storage_;
  };

}
