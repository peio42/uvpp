#include <array>
#include <cstring>
#include <memory>
#include <span>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

uv::buffer_view udp_alloc(uv::udp &, std::size_t) {
  static std::array<char, 1024> storage{};
  return uv::buffer_view{storage.data(), storage.size()};
}

}

TEST(Uvpp2Udp, sendsAndReceivesDatagrams) {
  uv::loop loop;
  uv::udp server(loop);
  uv::udp client(loop);
  uv::udp_send_request client_send_req;
  uv::udp_send_request server_send_req;

  int client_send_marker = 1;
  int server_send_marker = 2;
  client_send_req.user_data(client_send_marker);
  server_send_req.user_data(server_send_marker);

  bool server_read = false;
  bool client_read = false;
  bool client_send_done = false;
  bool server_send_done = false;
  bool server_closed = false;
  bool client_closed = false;

  server.bind(uv::ipv4{"127.0.0.1", 0});
  client.bind(uv::ipv4{"127.0.0.1", 0});

  server.receive_start(udp_alloc, [&](uv::udp &udp, uv::udp_receive_result received) {
    if (received.empty_event()) {
      return;
    }

    ASSERT_TRUE(received.ok());
    ASSERT_NE(received.address(), nullptr);
    auto bytes = received.bytes();
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(std::memcmp(bytes.data(), "ping", 4), 0);
    server_read = true;

    static char response[] = "pong";
    uv::buffer_view out{response, 4};
    udp.send(server_send_req, out, received.address(), [&](uv::udp_send_request &request, uv::result status) {
      ASSERT_TRUE(status);
      EXPECT_EQ(request.user_data<int>(), &server_send_marker);
      server_send_done = true;
    });
  });

  client.receive_start(udp_alloc, [&](uv::udp &udp, uv::udp_receive_result received) {
    if (received.empty_event()) {
      return;
    }

    ASSERT_TRUE(received.ok());
    auto bytes = received.bytes();
    ASSERT_EQ(bytes.size(), 4u);
    EXPECT_EQ(std::memcmp(bytes.data(), "pong", 4), 0);
    client_read = true;

    udp.close([&](uv::udp &) {
      client_closed = true;
    });
    server.close([&](uv::udp &) {
      server_closed = true;
    });
  });

  auto bound_addr = server.sockname();
  uv::ipv4 destination{"127.0.0.1", bound_addr.port()};

  static char payload[] = "ping";
  uv::buffer_view out{payload, 4};
  client.send(client_send_req, out, destination, [&](uv::udp_send_request &request, uv::result status) {
    ASSERT_TRUE(status);
    EXPECT_EQ(request.user_data<int>(), &client_send_marker);
    client_send_done = true;
  });

  loop.run();

  EXPECT_TRUE(server_read);
  EXPECT_TRUE(client_read);
  EXPECT_TRUE(client_send_done);
  EXPECT_TRUE(server_send_done);
  EXPECT_TRUE(server_closed);
  EXPECT_TRUE(client_closed);

  loop.close();
}

namespace {

bool static_udp_send_done = false;

void on_static_udp_send(uv::udp_send_request &, uv::result status) {
  EXPECT_TRUE(status);
  static_udp_send_done = true;
}

}

TEST(Uvpp2Udp, runsStaticSendCallback) {
  uv::loop loop;
  uv::udp server(loop);
  uv::udp client(loop);
  uv::udp_send_request send_req;
  bool server_read = false;
  bool server_closed = false;
  bool client_closed = false;
  static_udp_send_done = false;

  server.bind(uv::ipv4{"127.0.0.1", 0});
  client.bind(uv::ipv4{"127.0.0.1", 0});

  server.receive_start(udp_alloc, [&](uv::udp &, uv::udp_receive_result received) {
    if (received.empty_event()) {
      return;
    }

    ASSERT_TRUE(received.ok());
    server_read = true;
    server.close([&](uv::udp &) {
      server_closed = true;
    });
    client.close([&](uv::udp &) {
      client_closed = true;
    });
  });

  auto bound_addr2 = server.sockname();
  uv::ipv4 destination{"127.0.0.1", bound_addr2.port()};

  static char payload[] = "ping";
  uv::buffer_view out{payload, 4};
  client.send_static<on_static_udp_send>(send_req, out, destination);

  loop.run();

  EXPECT_TRUE(static_udp_send_done);
  EXPECT_TRUE(server_read);
  EXPECT_TRUE(server_closed);
  EXPECT_TRUE(client_closed);

  loop.close();
}

TEST(Uvpp2Udp, appliesSocketOptions) {
  uv::loop loop;
  uv::udp udp(loop);

  udp.bind(uv::ipv4{"127.0.0.1", 0});
  udp.set_broadcast(true);
  udp.set_ttl(64);
  udp.set_multicast_loop(true);
  udp.set_multicast_ttl(1);

  udp.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Udp, immediateSendFailureClearsCallback) {
  uv::loop loop;
  uv::udp udp(loop);
  uv::udp_send_request request;
  std::array payload{'f', 'a', 'i', 'l'};
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;

  EXPECT_THROW(udp.send(request, std::as_bytes(std::span{payload}), static_cast<const sockaddr *>(nullptr),
    [token](uv::udp_send_request&, uv::result) {}), uv::error);

  token.reset();
  EXPECT_TRUE(weak.expired());

  udp.close();
  loop.run();
  loop.close();
}
