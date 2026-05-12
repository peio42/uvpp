#include <filesystem>
#include <netinet/in.h>
#include <string>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

std::filesystem::path handle_path(const char *name) {
  return std::filesystem::temp_directory_path() /
         ("uvpp-handle-" + std::to_string(::getpid()) + "-" + name);
}

}

TEST(Uvpp2HandleMethods, exposesNativeFileDescriptorAndSocketBuffers) {
  uv::loop loop;
  uv::tcp tcp(loop);

  tcp.bind(uv::ipv4{"127.0.0.1", 0});

  EXPECT_NO_THROW(static_cast<void>(tcp.fileno()));

  auto send_size = tcp.send_buffer_size();
  auto receive_size = tcp.receive_buffer_size();
  EXPECT_GT(send_size, 0);
  EXPECT_GT(receive_size, 0);

  EXPECT_NO_THROW(tcp.send_buffer_size(send_size));
  EXPECT_NO_THROW(tcp.receive_buffer_size(receive_size));

  tcp.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2HandleMethods, exposesTcpOptionsAndSocknames) {
  uv::loop loop;
  uv::tcp tcp(loop);

  EXPECT_NO_THROW(tcp.no_delay(true));
  EXPECT_NO_THROW(tcp.keep_alive(false));
  EXPECT_NO_THROW(tcp.simultaneous_accepts(true));

  tcp.bind(uv::ipv4{"127.0.0.1", 0});

  auto tcp_addr = tcp.sockname();
  EXPECT_GT(tcp_addr.port(), 0);

  tcp.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2HandleMethods, exposesPipePathAndPendingState) {
  auto path = handle_path("pipe.sock");
  std::filesystem::remove(path);

  uv::loop loop;
  uv::pipe pipe(loop);

  pipe.bind(path.string());
  pipe.pending_instances(1);

  EXPECT_EQ(pipe.sockname(), path.string());
  EXPECT_EQ(pipe.pending_count(), 0);
  EXPECT_EQ(pipe.pending_type(), UV_UNKNOWN_HANDLE);

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
