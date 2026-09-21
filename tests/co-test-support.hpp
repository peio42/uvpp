#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/handles/pipe.hpp"
#include "uvpp/handles/tcp.hpp"
#include "uvpp/net/pipe_connection.hpp"
#include "uvpp/net/tcp_connection.hpp"

namespace uvpp::test {

template<class Handle>
concept spawn_handle_rebinds_result = requires(Handle &execution) {
  execution.template take_result<long>();
};

static_assert(!spawn_handle_rebinds_result<uv::co::spawn_handle<int>>);

inline bool loopback_tcp_is_permitted() {
  uv::loop loop;
  uv::tcp probe(loop);
  try {
    probe.bind(uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    probe.close();
    loop.run();
    loop.close();
    if (error.code().value() == UV_EPERM) {
      return false;
    }
    throw;
  }
  probe.close();
  loop.run();
  loop.close();
  return true;
}

inline std::filesystem::path filesystem_test_path(std::string_view name) {
  return std::filesystem::temp_directory_path() /
      ("uvpp-v3-filesystem-" + std::to_string(::getpid()) + "-" + std::string{name});
}

inline void write_filesystem_test_file(const std::filesystem::path &path, std::string_view contents) {
  std::ofstream stream{path, std::ios::binary | std::ios::trunc};
  ASSERT_TRUE(stream);
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  ASSERT_TRUE(stream);
}

struct connected_tcp_pair {
  uv::loop loop;
  uv::tcp listener{loop};
  std::unique_ptr<uv::tcp> peer;
  std::optional<uv::tcp_connection> client;
  std::optional<uv::co::spawn_handle<>> connect_execution;

  void connect() {
    listener.bind(uv::ipv4{"127.0.0.1", 0});
    const auto address = uv::ipv4{"127.0.0.1", listener.sockname().port()};
    listener.listen([&](uv::tcp &server, uv::status status) {
      EXPECT_TRUE(status);
      peer = std::make_unique<uv::tcp>(loop);
      EXPECT_NO_THROW(server.accept(*peer));
    });

    auto establish = [&]() -> uv::co::task<void> {
      client.emplace(co_await uv::tcp_connection::connect(address));
      loop.stop();
    };
    connect_execution.emplace(uv::co::spawn(loop, establish()));
    loop.run();
    connect_execution->rethrow_if_failed();
  }

  void close() {
    client.reset();
    if (peer && !peer->closing()) {
      peer->close();
    }
    if (!listener.closing()) {
      listener.close();
    }
    loop.run();
    loop.close();
  }
};

inline std::string v3_pipe_path() {
  return "/tmp/uvpp-v3-pipe-test-" + std::to_string(uv_os_getpid());
}

inline bool local_pipe_is_permitted() {
  const auto path = v3_pipe_path() + "-probe";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe probe(loop);
  try {
    probe.bind(path);
  } catch (const uv::error &error) {
    probe.close();
    loop.run();
    loop.close();
    std::filesystem::remove(path);
    if (error.code().value() == UV_EPERM) {
      return false;
    }
    throw;
  }
  probe.close();
  loop.run();
  loop.close();
  std::filesystem::remove(path);
  return true;
}

struct connected_pipe_pair {
  uv::loop loop;
  uv::pipe listener{loop};
  std::unique_ptr<uv::pipe> peer;
  std::optional<uv::pipe_connection> client;
  std::optional<uv::co::spawn_handle<>> connect_execution;
  std::string path{v3_pipe_path()};

  connected_pipe_pair() { std::filesystem::remove(path); }

  void connect() {
    listener.bind(path);
    listener.listen([&](uv::pipe &server, uv::status status) {
      EXPECT_TRUE(status);
      peer = std::make_unique<uv::pipe>(loop);
      EXPECT_NO_THROW(server.accept(*peer));
    });

    auto establish = [&]() -> uv::co::task<void> {
      client.emplace(co_await uv::pipe_connection::connect(path));
      loop.stop();
    };
    connect_execution.emplace(uv::co::spawn(loop, establish()));
    loop.run();
    connect_execution->rethrow_if_failed();
  }

  void close() {
    client.reset();
    if (peer && !peer->closing()) {
      peer->close();
    }
    if (!listener.closing()) {
      listener.close();
    }
    loop.run();
    loop.close();
    std::filesystem::remove(path);
  }
};

} // namespace uvpp::test
