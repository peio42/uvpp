#include <array>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

uvpp::buffer_view shutdown_alloc(uvpp::tcp &, std::size_t) {
  static std::array<char, 1024> storage{};
  return uvpp::buffer_view{storage.data(), storage.size()};
}

}

TEST(Uvpp2StreamRequests, tcpShutdownReportsCompletionAndRemoteEof) {
  uvpp::loop loop;
  uvpp::tcp server(loop);
  uvpp::tcp client(loop);
  uvpp::connect_request connect_req;
  uvpp::shutdown_request shutdown_req;

  int shutdown_marker = 42;
  shutdown_req.user_data(shutdown_marker);

  bool accepted_connection = false;
  bool client_connected = false;
  bool shutdown_done = false;
  bool server_eof = false;
  bool accepted_closed = false;
  bool client_closed = false;
  bool server_closed = false;

  server.bind(uvpp::ipv4{"127.0.0.1", 0});
  server.listen([&](uvpp::tcp &srv, uvpp::result status) {
    ASSERT_TRUE(status);
    accepted_connection = true;

    auto *accepted = new uvpp::tcp(loop);
    srv.accept(*accepted);
    accepted->read_start(shutdown_alloc, [&](uvpp::tcp &stream, uvpp::read_result read) {
      if (!read.eof()) {
        return;
      }

      server_eof = true;
      stream.close([&](uvpp::tcp &closed) {
        accepted_closed = true;
        delete &closed;
      });
      server.close([&](uvpp::tcp &) {
        server_closed = true;
      });
    });
  });

  sockaddr_in bound{};
  server.sockname(bound);
  uvpp::ipv4 connect_addr{"127.0.0.1", ntohs(bound.sin_port)};

  client.connect(connect_req, connect_addr, [&](uvpp::connect_request &, uvpp::result status) {
    ASSERT_TRUE(status);
    client_connected = true;

    client.shutdown(shutdown_req, [&](uvpp::shutdown_request &request, uvpp::result shutdown_status) {
      ASSERT_TRUE(shutdown_status);
      EXPECT_EQ(request.user_data<int>(), &shutdown_marker);
      shutdown_done = true;
      client.close([&](uvpp::tcp &) {
        client_closed = true;
      });
    });
  });

  loop.run();

  EXPECT_TRUE(accepted_connection);
  EXPECT_TRUE(client_connected);
  EXPECT_TRUE(shutdown_done);
  EXPECT_TRUE(server_eof);
  EXPECT_TRUE(accepted_closed);
  EXPECT_TRUE(client_closed);
  EXPECT_TRUE(server_closed);

  loop.close();
}

namespace {

uvpp::tcp *static_shutdown_client = nullptr;
bool static_shutdown_done = false;

void on_static_shutdown(uvpp::shutdown_request &, uvpp::result status) {
  EXPECT_TRUE(status);
  static_shutdown_done = true;
  static_shutdown_client->close();
}

}

TEST(Uvpp2StreamRequests, tcpShutdownRunsStaticCallback) {
  uvpp::loop loop;
  uvpp::tcp server(loop);
  uvpp::tcp client(loop);
  uvpp::connect_request connect_req;
  uvpp::shutdown_request shutdown_req;

  bool server_eof = false;
  bool server_closed = false;
  bool accepted_closed = false;
  static_shutdown_client = &client;
  static_shutdown_done = false;

  server.bind(uvpp::ipv4{"127.0.0.1", 0});
  server.listen([&](uvpp::tcp &srv, uvpp::result status) {
    ASSERT_TRUE(status);

    auto *accepted = new uvpp::tcp(loop);
    srv.accept(*accepted);
    accepted->read_start(shutdown_alloc, [&](uvpp::tcp &stream, uvpp::read_result read) {
      if (!read.eof()) {
        return;
      }

      server_eof = true;
      stream.close([&](uvpp::tcp &closed) {
        accepted_closed = true;
        delete &closed;
      });
      server.close([&](uvpp::tcp &) {
        server_closed = true;
      });
    });
  });

  sockaddr_in bound{};
  server.sockname(bound);
  uvpp::ipv4 connect_addr{"127.0.0.1", ntohs(bound.sin_port)};

  client.connect(connect_req, connect_addr, [&](uvpp::connect_request &, uvpp::result status) {
    ASSERT_TRUE(status);
    client.shutdown_static<on_static_shutdown>(shutdown_req);
  });

  loop.run();

  EXPECT_TRUE(static_shutdown_done);
  EXPECT_TRUE(server_eof);
  EXPECT_TRUE(accepted_closed);
  EXPECT_TRUE(server_closed);

  static_shutdown_client = nullptr;
  loop.close();
}
