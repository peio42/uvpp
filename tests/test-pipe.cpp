#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

uv::buffer_view pipe_alloc(uv::pipe &, std::size_t) {
  static std::array<char, 1024> storage{};
  return uv::buffer_view{storage.data(), storage.size()};
}

std::string pipe_path() {
  return "/tmp/uvpp2-pipe-test-" + std::to_string(getpid());
}

}

TEST(Uvpp2Pipe, acceptsReadsAndWrites) {
  auto path = pipe_path();
  std::filesystem::remove(path);

  uv::loop loop;
  uv::pipe server(loop);
  uv::pipe client(loop);
  uv::connect_request connect_req;
  uv::write_request client_write_req;
  uv::write_request server_write_req;

  std::unique_ptr<uv::pipe> accepted;
  bool accepted_connection = false;
  bool client_connected = false;
  bool server_read = false;
  bool client_write_done = false;
  bool server_write_done = false;
  bool accepted_closed = false;
  bool client_closed = false;
  bool server_closed = false;

  server.bind(path);
  server.listen([&](uv::pipe &srv, uv::result status) {
    ASSERT_TRUE(status);

    accepted_connection = true;
    accepted = std::make_unique<uv::pipe>(loop);
    srv.accept(*accepted);

    accepted->read_start(pipe_alloc, [&](uv::pipe &stream, uv::read_result read) {
      if (read.eof()) {
        return;
      }

      ASSERT_TRUE(read.ok());
      ASSERT_EQ(read.count(), 4);
      server_read = true;

      static char response[] = "pong";
      uv::buffer_view out{response, 4};
      stream.write(server_write_req, out, [&](uv::write_request &, uv::result write_status) {
        ASSERT_TRUE(write_status);
        server_write_done = true;
        stream.close([&](uv::pipe &) {
          accepted_closed = true;
        });
      });
    });
  });

  client.connect(connect_req, path, [&](uv::connect_request &, uv::result status) {
    ASSERT_TRUE(status);
    client_connected = true;

    static char payload[] = "ping";
    uv::buffer_view out{payload, 4};
    client.write(client_write_req, out, [&](uv::write_request &, uv::result write_status) {
      ASSERT_TRUE(write_status);
      client_write_done = true;

      client.read_start(pipe_alloc, [&](uv::pipe &stream, uv::read_result read) {
        if (read.eof()) {
          return;
        }

        ASSERT_TRUE(read.ok());
        auto bytes = read.bytes();
        ASSERT_EQ(bytes.size(), 4u);
        EXPECT_EQ(std::memcmp(bytes.data(), "pong", 4), 0);
        stream.close([&](uv::pipe &) {
          client_closed = true;
        });
        server.close([&](uv::pipe &) {
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
  std::filesystem::remove(path);
}

static uv::pipe *static_pipe_client = nullptr;
static bool static_pipe_failed = false;

static void on_static_pipe_connect(uv::connect_request &, uv::result status) {
  EXPECT_FALSE(status);
  static_pipe_failed = true;
  static_pipe_client->close();
}

TEST(Uvpp2Pipe, reportsStaticConnectFailureCallback) {
  auto path = pipe_path() + "-static";
  std::filesystem::remove(path);

  uv::loop loop;
  uv::pipe client(loop);
  uv::connect_request connect_req;
  static_pipe_client = &client;
  static_pipe_failed = false;

  client.connect_static<on_static_pipe_connect>(connect_req, path);

  loop.run();
  EXPECT_TRUE(static_pipe_failed);
  static_pipe_client = nullptr;
  loop.close();
  std::filesystem::remove(path);
}

#if UVPP_HAS_PIPE_BIND2 && UVPP_HAS_PIPE_CONNECT2
TEST(Uvpp2Pipe, bindsAndConnectsWithPipeNameFlags) {
  auto path = pipe_path() + "-bind2";
  std::filesystem::remove(path);

  uv::loop loop;
  uv::pipe server(loop);
  uv::pipe client(loop);
  uv::connect_request connect_req;
  std::unique_ptr<uv::pipe> accepted;

  bool accepted_connection = false;
  bool client_connected = false;
  bool accepted_closed = false;
  bool client_closed = false;
  bool server_closed = false;

  server.bind(path, uv::pipe_name_flag::no_truncate);
  server.listen([&](uv::pipe &srv, uv::result status) {
    ASSERT_TRUE(status);
    accepted_connection = true;

    accepted = std::make_unique<uv::pipe>(loop);
    srv.accept(*accepted);

    accepted->close([&](uv::pipe &) {
      accepted_closed = true;
    });
    srv.close([&](uv::pipe &) {
      server_closed = true;
    });
  });

  client.connect(connect_req, path, uv::pipe_name_flag::no_truncate,
    [&](uv::connect_request &, uv::result status) {
      ASSERT_TRUE(status);
      client_connected = true;
      client.close([&](uv::pipe &) {
        client_closed = true;
      });
    });

  loop.run();

  EXPECT_TRUE(accepted_connection);
  EXPECT_TRUE(client_connected);
  EXPECT_TRUE(accepted_closed);
  EXPECT_TRUE(client_closed);
  EXPECT_TRUE(server_closed);

  loop.close();
  std::filesystem::remove(path);
}

TEST(Uvpp2Pipe, connect2ImmediateFailureClearsCallback) {
  uv::loop loop;
  uv::pipe client(loop);
  uv::connect_request connect_req;

  bool callback_called = false;

  EXPECT_THROW(client.connect(connect_req, "uvpp-invalid-flags", 1u << 8,
    [&](uv::connect_request &, uv::result) {
      callback_called = true;
    }), uv::error);

  client.close();
  loop.run();

  EXPECT_FALSE(callback_called);
  loop.close();
}
#endif

TEST(Uvpp2Pipe, writeWithHandleSendsStreamOverIpc) {
  auto path = pipe_path() + "-ipc";
  std::filesystem::remove(path);

  uv::loop loop;
  uv::pipe ipc_server(loop);        // ipc=false: only accepts connections
  uv::pipe ipc_client(loop, true);  // ipc=true: sends handles via write_with_handle
  uv::connect_request connect_req;
  uv::write_request write_req;

  uv::tcp tcp_to_pass(loop);
  tcp_to_pass.bind(uv::ipv4{"127.0.0.1", 0});

  bool data_received   = false;
  bool handle_received = false;

  ipc_server.bind(path);
  ipc_server.listen([&](uv::pipe &srv, uv::result status) {
    ASSERT_TRUE(status);

    auto *accepted = new uv::pipe(loop, true);
    srv.accept(*accepted);
    srv.close();

    accepted->read_start(pipe_alloc, [&](uv::pipe &ipc, uv::read_result read) {
      if (read.eof()) {
        ipc.close([](uv::pipe &p) { delete &p; });
        return;
      }
      ASSERT_TRUE(read.ok());
      data_received = true;

      if (ipc.pending_count() > 0) {
        EXPECT_EQ(ipc.pending_type(), uv::handle_type::tcp);
        auto *received = new uv::tcp(loop);
        ipc.accept(*received);
        handle_received = true;
        received->close([](uv::tcp &t) { delete &t; });
      }

      ipc.read_stop();
      ipc.close([](uv::pipe &p) { delete &p; });
    });
  });

  ipc_client.connect(connect_req, path, [&](uv::connect_request &, uv::result status) {
    ASSERT_TRUE(status);
    static char msg[] = "x";
    uv::buffer_view buf{msg, 1};
    ipc_client.write_with_handle(write_req,
      std::span<const uv::buffer_view>{&buf, 1},
      tcp_to_pass,
      [&](uv::write_request &, uv::result wr) {
        ASSERT_TRUE(wr);
        ipc_client.close();
        tcp_to_pass.close();
      });
  });

  loop.run();

  EXPECT_TRUE(data_received);
  EXPECT_TRUE(handle_received);

  loop.close();
  std::filesystem::remove(path);
}

TEST(Uvpp2Pipe, writeWithHandleNowReturnsErrorOnUnconnectedPipe) {
  uv::loop loop;
  uv::pipe ipc(loop, true);
  uv::tcp handle(loop);

  static char msg[] = "x";
  uv::buffer_view buf{msg, 1};
  auto r = ipc.write_with_handle_now(std::span<const uv::buffer_view>{&buf, 1}, handle);

  EXPECT_TRUE(r.has_error());
  EXPECT_FALSE(r.ok());

  ipc.close();
  handle.close();
  loop.run();
  loop.close();
}
