#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include "platform-support.hpp"

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

uv::buffer_view pipe_alloc(uv::raw::pipe &, std::size_t) {
  static std::array<char, 1024> storage{};
  return uv::buffer_view{storage.data(), storage.size()};
}

std::string pipe_path() {
  return uvpp::test::pipe_path("raw");
}

}

TEST(Uvpp2Pipe, acceptsReadsAndWrites) {
  auto path = pipe_path();
  std::filesystem::remove(path);

  uv::loop loop;
  uv::raw::pipe server(loop);
  uv::raw::pipe client(loop);
  uv::raw::connect_request connect_req;
  uv::raw::write_request client_write_req;
  uv::raw::write_request server_write_req;

  std::unique_ptr<uv::raw::pipe> accepted;
  bool accepted_connection = false;
  bool client_connected = false;
  bool server_read = false;
  bool client_write_done = false;
  bool server_write_done = false;
  bool accepted_closed = false;
  bool client_closed = false;
  bool server_closed = false;

  server.bind(path).throw_if_error();
  server.listen([&](uv::raw::pipe &srv, uv::status status) {
    ASSERT_TRUE(status);

    accepted_connection = true;
    accepted = std::make_unique<uv::raw::pipe>(loop);
    srv.accept(*accepted).throw_if_error();

    accepted->read_start(pipe_alloc, [&](uv::raw::pipe &stream, uv::read_result read) {
      if (read.eof()) {
        return;
      }

      ASSERT_TRUE(read.ok());
      ASSERT_EQ(read.count(), 4);
      server_read = true;

      static char response[] = "pong";
      uv::buffer_view out{response, 4};
      stream.write(server_write_req, out, [&](uv::raw::write_request &, uv::status write_status) {
        ASSERT_TRUE(write_status);
        server_write_done = true;
        stream.close([&](uv::raw::pipe &) {
          accepted_closed = true;
        });
      }).throw_if_error();
    }).throw_if_error();
  }).throw_if_error();

  client.connect(connect_req, path, [&](uv::raw::connect_request &, uv::status status) {
    ASSERT_TRUE(status);
    client_connected = true;

    static char payload[] = "ping";
    uv::buffer_view out{payload, 4};
    client.write(client_write_req, out, [&](uv::raw::write_request &, uv::status write_status) {
      ASSERT_TRUE(write_status);
      client_write_done = true;

      client.read_start(pipe_alloc, [&](uv::raw::pipe &stream, uv::read_result read) {
        if (read.eof()) {
          return;
        }

        ASSERT_TRUE(read.ok());
        auto bytes = read.bytes();
        ASSERT_EQ(bytes.size(), 4u);
        EXPECT_EQ(std::memcmp(bytes.data(), "pong", 4), 0);
        stream.close([&](uv::raw::pipe &) {
          client_closed = true;
        });
        server.close([&](uv::raw::pipe &) {
          server_closed = true;
        });
      }).throw_if_error();
    }).throw_if_error();
  }).throw_if_error();

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

static uv::raw::pipe *static_pipe_client = nullptr;
static bool static_pipe_failed = false;

static void on_static_pipe_connect(uv::raw::connect_request &, uv::status status) {
  EXPECT_FALSE(status);
  static_pipe_failed = true;
  static_pipe_client->close();
}

TEST(Uvpp2Pipe, reportsStaticConnectFailureCallback) {
  auto path = pipe_path() + "-static";
  std::filesystem::remove(path);

  uv::loop loop;
  uv::raw::pipe client(loop);
  uv::raw::connect_request connect_req;
  static_pipe_client = &client;
  static_pipe_failed = false;

  client.connect_static<on_static_pipe_connect>(connect_req, path).throw_if_error();

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
  uv::raw::pipe server(loop);
  uv::raw::pipe client(loop);
  uv::raw::connect_request connect_req;
  std::unique_ptr<uv::raw::pipe> accepted;

  bool accepted_connection = false;
  bool client_connected = false;
  bool accepted_closed = false;
  bool client_closed = false;
  bool server_closed = false;

  server.bind(path, uv::pipe_name_flag::no_truncate).throw_if_error();
  server.listen([&](uv::raw::pipe &srv, uv::status status) {
    ASSERT_TRUE(status);
    accepted_connection = true;

    accepted = std::make_unique<uv::raw::pipe>(loop);
    srv.accept(*accepted).throw_if_error();

    accepted->close([&](uv::raw::pipe &) {
      accepted_closed = true;
    });
    srv.close([&](uv::raw::pipe &) {
      server_closed = true;
    });
  }).throw_if_error();

  client.connect(connect_req, path, uv::pipe_name_flag::no_truncate,
    [&](uv::raw::connect_request &, uv::status status) {
      ASSERT_TRUE(status);
      client_connected = true;
      client.close([&](uv::raw::pipe &) {
        client_closed = true;
      });
    }).throw_if_error();

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
  uv::raw::pipe client(loop);
  uv::raw::connect_request connect_req;

  bool callback_called = false;

  EXPECT_FALSE(client.connect(connect_req, "uvpp-invalid-flags", 1u << 8,
    [&](uv::raw::connect_request &, uv::status) {
      callback_called = true;
    }));

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
  uv::raw::pipe ipc_server(loop);        // ipc=false: only accepts connections
  uv::raw::pipe ipc_client(loop, true);  // ipc=true: sends handles via write_with_handle
  uv::raw::connect_request connect_req;
  uv::raw::write_request write_req;

  uv::raw::tcp tcp_to_pass(loop);
  tcp_to_pass.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();

  bool data_received   = false;
  bool handle_received = false;

  ipc_server.bind(path).throw_if_error();
  ipc_server.listen([&](uv::raw::pipe &srv, uv::status status) {
    ASSERT_TRUE(status);

    auto *accepted = new uv::raw::pipe(loop, true);
    srv.accept(*accepted).throw_if_error();
    srv.close();

    accepted->read_start(pipe_alloc, [&](uv::raw::pipe &ipc, uv::read_result read) {
      if (read.eof()) {
        ipc.close([](uv::raw::pipe &p) { delete &p; });
        return;
      }
      ASSERT_TRUE(read.ok());
      data_received = true;

      if (ipc.pending_count() > 0) {
        EXPECT_EQ(ipc.pending_type(), uv::handle_type::tcp);
        auto *received = new uv::raw::tcp(loop);
        ipc.accept(*received).throw_if_error();
        handle_received = true;
        received->close([](uv::raw::tcp &t) { delete &t; });
      }

      ipc.read_stop().throw_if_error();
      ipc.close([](uv::raw::pipe &p) { delete &p; });
    }).throw_if_error();
  }).throw_if_error();

  ipc_client.connect(connect_req, path, [&](uv::raw::connect_request &, uv::status status) {
    ASSERT_TRUE(status);
    static char msg[] = "x";
    uv::buffer_view buf{msg, 1};
    ipc_client.write_with_handle(write_req,
      std::span<const uv::buffer_view>{&buf, 1},
      tcp_to_pass,
      [&](uv::raw::write_request &, uv::status wr) {
        ASSERT_TRUE(wr);
        ipc_client.close();
        tcp_to_pass.close();
      }).throw_if_error();
  }).throw_if_error();

  loop.run();

  EXPECT_TRUE(data_received);
  EXPECT_TRUE(handle_received);

  loop.close();
  std::filesystem::remove(path);
}

TEST(Uvpp2Pipe, writeWithHandleNowReportsUnconnectedPipeOutcome) {
  uv::loop loop;
  uv::raw::pipe ipc(loop, true);
  uv::raw::tcp handle(loop);

  static char msg[] = "x";
  uv::buffer_view buf{msg, 1};
  auto r = ipc.write_with_handle_now(std::span<const uv::buffer_view>{&buf, 1}, handle);

#ifdef _WIN32
  // Native immediate handle transfer is unsupported on Windows. libuv returns
  // EAGAIN without attempting the write, even for an unconnected pipe.
  EXPECT_TRUE(r.would_block());
  EXPECT_FALSE(r.has_error());
  EXPECT_EQ(r.raw(), UV_EAGAIN);
  EXPECT_FALSE(r.error_code());
#else
  EXPECT_TRUE(r.has_error());
  EXPECT_FALSE(r.would_block());
  EXPECT_TRUE(r.error_code());
#endif
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.bytes_written(), 0u);

  ipc.close();
  handle.close();
  loop.run();
  loop.close();
}
