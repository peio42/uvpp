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
