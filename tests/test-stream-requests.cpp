#include <array>
#include <memory>
#include <span>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

uv::buffer_view shutdown_alloc(uv::tcp &, std::size_t) {
  static std::array<char, 1024> storage{};
  return uv::buffer_view{storage.data(), storage.size()};
}

}

TEST(Uvpp2StreamRequests, tcpShutdownReportsCompletionAndRemoteEof) {
  uv::loop loop;
  uv::tcp server(loop);
  uv::tcp client(loop);
  uv::connect_request connect_req;
  uv::shutdown_request shutdown_req;

  int shutdown_marker = 42;
  shutdown_req.user_data(shutdown_marker);

  bool accepted_connection = false;
  bool client_connected = false;
  bool shutdown_done = false;
  bool server_eof = false;
  bool accepted_closed = false;
  bool client_closed = false;
  bool server_closed = false;

  server.bind(uv::ipv4{"127.0.0.1", 0});
  server.listen([&](uv::tcp &srv, uv::result status) {
    ASSERT_TRUE(status);
    accepted_connection = true;

    auto *accepted = new uv::tcp(loop);
    srv.accept(*accepted);
    accepted->read_start(shutdown_alloc, [&](uv::tcp &stream, uv::read_result read) {
      if (!read.eof()) {
        return;
      }

      server_eof = true;
      stream.close([&](uv::tcp &closed) {
        accepted_closed = true;
        delete &closed;
      });
      server.close([&](uv::tcp &) {
        server_closed = true;
      });
    });
  });

  auto bound = server.sockname();
  uv::ipv4 connect_addr{"127.0.0.1", bound.port()};

  client.connect(connect_req, connect_addr, [&](uv::connect_request &, uv::result status) {
    ASSERT_TRUE(status);
    client_connected = true;

    client.shutdown(shutdown_req, [&](uv::shutdown_request &request, uv::result shutdown_status) {
      ASSERT_TRUE(shutdown_status);
      EXPECT_EQ(request.user_data<int>(), &shutdown_marker);
      shutdown_done = true;
      client.close([&](uv::tcp &) {
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

uv::tcp *static_shutdown_client = nullptr;
bool static_shutdown_done = false;

void on_static_shutdown(uv::shutdown_request &, uv::result status) {
  EXPECT_TRUE(status);
  static_shutdown_done = true;
  static_shutdown_client->close();
}

}

TEST(Uvpp2StreamRequests, tcpShutdownRunsStaticCallback) {
  uv::loop loop;
  uv::tcp server(loop);
  uv::tcp client(loop);
  uv::connect_request connect_req;
  uv::shutdown_request shutdown_req;

  bool server_eof = false;
  bool server_closed = false;
  bool accepted_closed = false;
  static_shutdown_client = &client;
  static_shutdown_done = false;

  server.bind(uv::ipv4{"127.0.0.1", 0});
  server.listen([&](uv::tcp &srv, uv::result status) {
    ASSERT_TRUE(status);

    auto *accepted = new uv::tcp(loop);
    srv.accept(*accepted);
    accepted->read_start(shutdown_alloc, [&](uv::tcp &stream, uv::read_result read) {
      if (!read.eof()) {
        return;
      }

      server_eof = true;
      stream.close([&](uv::tcp &closed) {
        accepted_closed = true;
        delete &closed;
      });
      server.close([&](uv::tcp &) {
        server_closed = true;
      });
    });
  });

  auto bound2 = server.sockname();
  uv::ipv4 connect_addr{"127.0.0.1", bound2.port()};

  client.connect(connect_req, connect_addr, [&](uv::connect_request &, uv::result status) {
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

TEST(Uvpp2StreamRequests, immediateWriteFailureClearsCallback) {
  uv::loop loop;
  uv::tcp tcp(loop);
  uv::write_request request;
  std::array payload{'f', 'a', 'i', 'l'};
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;

  EXPECT_THROW(tcp.write(request, std::as_bytes(std::span{payload}),
    [token](uv::write_request&, uv::result) {}), uv::error);

  token.reset();
  EXPECT_TRUE(weak.expired());

  tcp.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2StreamRequests, immediateShutdownFailureClearsCallback) {
  uv::loop loop;
  uv::tcp tcp(loop);
  uv::shutdown_request request;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;

  EXPECT_THROW(tcp.shutdown(request, [token](uv::shutdown_request&, uv::result) {}), uv::error);

  token.reset();
  EXPECT_TRUE(weak.expired());

  tcp.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2StreamRequests, writeNowSendsBytesOnConnectedTcp) {
  uv::loop loop;
  uv::tcp server(loop);
  uv::tcp client(loop);
  uv::connect_request connect_req;

  std::size_t bytes_written = 0;
  std::size_t bytes_read    = 0;
  bool write_ok = false;

  static std::array<char, 1024> read_buf{};
  auto alloc = [](uv::tcp &, std::size_t) {
    return uv::buffer_view{read_buf.data(), read_buf.size()};
  };

  server.bind(uv::ipv4{"127.0.0.1", 0});
  server.listen([&](uv::tcp &srv, uv::result status) {
    ASSERT_TRUE(status);
    auto *accepted = new uv::tcp(loop);
    srv.accept(*accepted);
    srv.close();
    accepted->read_start(alloc, [&](uv::tcp &conn, uv::read_result read) {
      if (read.eof()) {
        conn.close([](uv::tcp &c) { delete &c; });
        return;
      }
      ASSERT_TRUE(read.ok());
      bytes_read += static_cast<std::size_t>(read.count());
    });
  });

  auto bound = server.sockname();
  client.connect(connect_req, uv::ipv4{"127.0.0.1", bound.port()},
    [&](uv::connect_request &, uv::result status) {
      ASSERT_TRUE(status);
      static char payload[] = "hello";
      auto r = client.write_now(std::as_bytes(std::span{payload, 5}));
      write_ok         = r.ok();
      bytes_written    = r.bytes_written();
      client.close();
    });

  loop.run();
  loop.close();

  EXPECT_TRUE(write_ok);
  EXPECT_EQ(bytes_written, 5u);
  EXPECT_EQ(bytes_read, 5u);
}

TEST(Uvpp2StreamRequests, writeNowReturnsErrorOnUnconnectedStream) {
  uv::loop loop;
  uv::tcp tcp(loop);

  static char payload[] = "test";
  auto r = tcp.write_now(std::as_bytes(std::span{payload, 4}));

  EXPECT_TRUE(r.has_error());
  EXPECT_FALSE(r.ok());
  EXPECT_FALSE(r.would_block());
  EXPECT_TRUE(r.error_code());

  tcp.close();
  loop.run();
  loop.close();
}
