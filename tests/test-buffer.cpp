#include <array>
#include <cstddef>
#include <cstring>
#include <span>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Buffer, ownedBufferOwnsBytesAndProducesExplicitView) {
  uvpp::owned_buffer buffer{4};
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

  uvpp::owned_buffer buffer{std::as_bytes(std::span{input})};

  auto bytes = buffer.bytes();
  EXPECT_EQ(bytes.size(), 4u);
  EXPECT_EQ(std::memcmp(buffer.view().data(), "pong", 4), 0);
}
