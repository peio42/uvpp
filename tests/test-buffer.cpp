#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Buffer, ownedBufferOwnsBytesAndProducesExplicitView) {
  uv::owned_buffer buffer{4};
  std::memcpy(buffer.data(), "ping", 4);

  auto view = buffer.view();

  EXPECT_EQ(buffer.size(), 4u);
  EXPECT_FALSE(buffer.empty());
  EXPECT_EQ(view.size(), 4u);
  EXPECT_EQ(view.data(), reinterpret_cast<char *>(buffer.data()));
  EXPECT_EQ(std::memcmp(view.data(), "ping", 4), 0);
}

TEST(Uvpp2Buffer, ownedBufferCopiesByteRanges) {
  std::array input{'p', 'o', 'n', 'g'};

  uv::owned_buffer buffer{std::as_bytes(std::span{input})};

  auto bytes = buffer.bytes();
  EXPECT_EQ(bytes.size(), 4u);
  EXPECT_EQ(std::memcmp(buffer.view().data(), "pong", 4), 0);
}

TEST(Uvpp2Buffer, preservesNativeLengthsBeyondUnsignedInt) {
  if constexpr (std::numeric_limits<uv::detail::native_buffer_length>::digits > std::numeric_limits<unsigned int>::digits) {
    char storage{};
    const auto length = static_cast<std::size_t>(std::numeric_limits<unsigned int>::max()) + 3;

    uv::buffer_view view{&storage, length};
    EXPECT_EQ(view.size(), length);
    EXPECT_EQ(view.native()->len, length);

    uv_buf_t raw{};
    raw.base = &storage;
    raw.len = length;
    EXPECT_EQ(uv::buffer_view::from_native(raw).size(), length);
  }
}

TEST(Uvpp2Buffer, rejectsLengthsAndCountsOutsideNativeLimits) {
  if constexpr (std::numeric_limits<std::size_t>::digits > std::numeric_limits<uv::detail::native_buffer_length>::digits) {
    char storage{};
    const auto length = static_cast<std::size_t>(std::numeric_limits<uv::detail::native_buffer_length>::max()) + 1;
    EXPECT_THROW((uv::buffer_view{&storage, length}), std::length_error);
  }

  if constexpr (std::numeric_limits<std::size_t>::digits > std::numeric_limits<unsigned int>::digits) {
    const auto count = static_cast<std::size_t>(std::numeric_limits<unsigned int>::max()) + 1;
    EXPECT_THROW(uv::detail::checked_buffer_count(count), std::length_error);
  }
}
