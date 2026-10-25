#include <filesystem>
#include <string>
#include "platform-support.hpp"

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2HandleMethods, exposesNativeFileDescriptorAndSocketBuffers) {
  uv::loop loop;
  uv::raw::tcp tcp(loop);

  tcp.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();

  EXPECT_TRUE(tcp.fileno());

  auto send_size = tcp.send_buffer_size();
  auto receive_size = tcp.receive_buffer_size();
  ASSERT_TRUE(send_size);
  EXPECT_GT(send_size.value(), 0);
  ASSERT_TRUE(receive_size);
  EXPECT_GT(receive_size.value(), 0);

  EXPECT_TRUE(tcp.send_buffer_size(send_size.value()));
  EXPECT_TRUE(tcp.receive_buffer_size(receive_size.value()));

  tcp.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2HandleMethods, exposesTcpOptionsAndSocknames) {
  uv::loop loop;
  uv::raw::tcp tcp(loop);

  EXPECT_TRUE(tcp.no_delay(true));
  EXPECT_TRUE(tcp.keep_alive(false));
  EXPECT_TRUE(tcp.simultaneous_accepts(true));

  tcp.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();

  auto tcp_addr = tcp.sockname().value();
  EXPECT_GT(tcp_addr.port(), 0);

  tcp.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2HandleMethods, exposesPipePathAndPendingState) {
  auto path = std::filesystem::path{uvpp::test::pipe_path("methods")};
  std::filesystem::remove(path);

  uv::loop loop;
  uv::pipe pipe(loop);

  pipe.bind(path.string());
  pipe.pending_instances(1);

  EXPECT_EQ(pipe.sockname(), path.string());
  EXPECT_EQ(pipe.pending_count(), 0);
  EXPECT_EQ(pipe.pending_type(), uv::handle_type::unknown);

  pipe.close();
  loop.run();
  loop.close();

  std::filesystem::remove(path);
}

TEST(Uvpp2HandleMethods, exposesUdpConnectedPeerAndQueueState) {
  uv::loop loop;
  uv::udp udp(loop);

  udp.bind(uv::ipv4{"127.0.0.1", 0});
  udp.connect(uv::ipv4{"127.0.0.1", 9});

  auto udp_peer = udp.peername();
  EXPECT_EQ(udp_peer.port(), 9);

  EXPECT_EQ(udp.send_queue_size(), 0u);
  EXPECT_EQ(udp.send_queue_count(), 0u);

  EXPECT_NO_THROW(udp.disconnect());

  udp.close();
  loop.run();
  loop.close();
}
