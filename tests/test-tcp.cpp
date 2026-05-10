#include <array>
#include <cstring>
#include <memory>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

uvpp::buffer_view test_alloc(uvpp::tcp &, std::size_t) {
  static std::array<char, 1024> storage{};
  return uvpp::buffer_view{storage.data(), storage.size()};
}

}

TEST(Uvpp2Tcp, acceptsReadsAndWrites) {
  uvpp::loop loop;
  uvpp::tcp server(loop);
  uvpp::tcp client(loop);
  uvpp::connect_request connect_req;
  uvpp::write_request client_write_req;
  uvpp::write_request server_write_req;

  int server_marker = 1;
  int client_marker = 2;
  int connect_marker = 3;
  int client_write_marker = 4;
  int server_write_marker = 5;

  server.user_data(server_marker);
  client.user_data(client_marker);
  connect_req.user_data(connect_marker);
  client_write_req.user_data(client_write_marker);
  server_write_req.user_data(server_write_marker);

  std::unique_ptr<uvpp::tcp> accepted;
  bool accepted_connection = false;
  bool client_connected = false;
  bool server_read = false;
  bool client_write_done = false;
  bool server_write_done = false;
  bool accepted_closed = false;
  bool client_closed = false;
  bool server_closed = false;

  uvpp::ipv4 bind_addr{"127.0.0.1", 0};
  server.bind(bind_addr);

  server.listen([&](uvpp::tcp &srv, uvpp::result status) {
    ASSERT_TRUE(status);
    EXPECT_EQ(srv.user_data<int>(), &server_marker);

    accepted_connection = true;
    accepted = std::make_unique<uvpp::tcp>(loop);
    srv.accept(*accepted);
    accepted->user_data(server_marker);

    accepted->read_start(test_alloc, [&](uvpp::tcp &stream, uvpp::read_result read) {
      if (read.eof()) {
        return;
      }

      ASSERT_TRUE(read.ok());
      ASSERT_EQ(read.count(), 4);
      EXPECT_EQ(stream.user_data<int>(), &server_marker);
      server_read = true;

      static char response[] = "pong";
      uvpp::buffer_view out{response, 4};
      stream.write(server_write_req, out, [&](uvpp::write_request &request, uvpp::result write_status) {
        ASSERT_TRUE(write_status);
        EXPECT_EQ(request.user_data<int>(), &server_write_marker);
        server_write_done = true;
        stream.close([&](uvpp::tcp &) {
          accepted_closed = true;
        });
      });
    });
  });

  sockaddr_in bound{};
  server.sockname(bound);
  uvpp::ipv4 connect_addr{"127.0.0.1", ntohs(bound.sin_port)};

  client.connect(connect_req, connect_addr, [&](uvpp::connect_request &request, uvpp::result status) {
    ASSERT_TRUE(status);
    EXPECT_EQ(request.user_data<int>(), &connect_marker);
    client_connected = true;

    static char payload[] = "ping";
    uvpp::buffer_view out{payload, 4};
    client.write(client_write_req, out, [&](uvpp::write_request &request, uvpp::result write_status) {
      ASSERT_TRUE(write_status);
      EXPECT_EQ(request.user_data<int>(), &client_write_marker);
      client_write_done = true;

      client.read_start(test_alloc, [&](uvpp::tcp &stream, uvpp::read_result read) {
        if (read.eof()) {
          return;
        }

        ASSERT_TRUE(read.ok());
        ASSERT_EQ(read.count(), 4);
        EXPECT_EQ(stream.user_data<int>(), &client_marker);
        auto bytes = read.bytes();
        ASSERT_EQ(bytes.size(), 4u);
        EXPECT_EQ(std::memcmp(bytes.data(), "pong", 4), 0);
        stream.close([&](uvpp::tcp &) {
          client_closed = true;
        });
        server.close([&](uvpp::tcp &) {
          server_closed = true;
        });
      });
    });
  });

  loop.run();

  EXPECT_TRUE(accepted_connection);
  EXPECT_TRUE(client_connected);
  EXPECT_TRUE(server_read);
  EXPECT_TRUE(client_write_done);
  EXPECT_TRUE(server_write_done);
  EXPECT_TRUE(accepted_closed);
  EXPECT_TRUE(client_closed);
  EXPECT_TRUE(server_closed);

  loop.close();
}
