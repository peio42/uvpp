#include <vector>
#include "gtest/gtest.h"
#include "uvpp/uv.hpp"


TEST(StandardLayout, standardlayout) {
  EXPECT_TRUE(std::is_standard_layout_v<uv::Loop>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Handle>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Timer>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Tcp>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Prepare>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Check>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Idle>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Async>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Poll>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Signal>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Process>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Stream>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Tcp>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Pipe>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Tty>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Udp>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::FsEvent>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::FsPoll>);


  EXPECT_TRUE(std::is_standard_layout_v<uv::Stream::ConnectRq>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Stream::ShutdownRq>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Stream::WriteRq>);

  EXPECT_TRUE(std::is_standard_layout_v<uv::Tcp::ConnectRq>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Tcp::ShutdownRq>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Tcp::WriteRq>);

  EXPECT_TRUE(std::is_standard_layout_v<uv::Pipe::ConnectRq>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Pipe::ShutdownRq>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Pipe::WriteRq>);

  EXPECT_TRUE(std::is_standard_layout_v<uv::Tty::ConnectRq>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Tty::ShutdownRq>);
  EXPECT_TRUE(std::is_standard_layout_v<uv::Tty::WriteRq>);

  EXPECT_TRUE(std::is_standard_layout_v<uv::Udp::SendRq>);


  EXPECT_TRUE(std::is_standard_layout_v<uv::Thread::Options>);
}
