#include "co-test-support.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "uvpp/co/resource_scope.hpp"
#include "uvpp/co/sleep.hpp"
#include "uvpp/co/task_scope.hpp"
#include "uvpp/net/pipe_listener.hpp"
#include "uvpp/net/tcp_listener.hpp"

using namespace std::chrono_literals;
using namespace uvpp::test;

TEST(UvppV3CoroutineDeathTest, tcpConnectionDestructionWithActiveReadTerminates) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  EXPECT_DEATH(([&] {
    connected_tcp_pair pair;
    pair.connect();
    std::array<std::byte, 8> buffer{};
    auto reader = [&]() -> uv::co::task<void> {
      (void)co_await pair.client->read_some(buffer);
    };
    auto execution = uv::co::spawn(pair.loop, reader());
    pair.client.reset();
  }()), "");
}

TEST(UvppV3CoroutineDeathTest, taskScopeDestructionWithoutJoinTerminates) {
  EXPECT_DEATH(([&] {
    uv::loop loop;
    auto child = []() -> uv::co::task<void> {
      co_await uv::co::sleep_for(1ms);
    };
    auto parent = [&]() -> uv::co::task<void> {
      uv::co::task_scope scope(loop);
      scope.spawn(child());
      co_return;
    };
    auto execution = uv::co::spawn(loop, parent());
  }()), "");
}

TEST(UvppV3CoroutineDeathTest, abandonedActiveSpawnFailureTerminates) {
  EXPECT_DEATH(([&] {
    uv::loop loop;
    auto worker = []() -> uv::co::task<void> {
      co_await uv::co::sleep_for(0ms);
      throw std::runtime_error{"unobserved abandoned root failure"};
    };
    {
      auto execution = uv::co::spawn(loop, worker());
    }
    loop.run();
  }()), "");
}

TEST(UvppV3CoroutineDeathTest, resourceScopeDestructionBeforeFinishTerminates) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  EXPECT_DEATH(([&] {
    connected_tcp_pair pair;
    pair.connect();
    {
      uv::co::resource_scope resources(pair.loop);
      auto connection = resources.own(std::move(*pair.client));
      pair.client.reset();
      (void)connection;
    }
  }()), "");
}

TEST(UvppV3CoroutineDeathTest, tcpConnectionDestructionWithActiveWriteTerminates) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  EXPECT_DEATH(([&] {
    connected_tcp_pair pair;
    pair.connect();
    std::string data{"pending write"};
    auto writer = [&]() -> uv::co::task<void> {
      co_await pair.client->write(data);
    };
    auto execution = uv::co::spawn(pair.loop, writer());
    pair.client.reset();
  }()), "");
}

TEST(UvppV3CoroutineDeathTest, tcpConnectionDestructionWithActivePipeHandleExportTerminates) {
  if (!local_pipe_is_permitted() || !loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "local pipe or loopback TCP is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-handle-export-death";
  std::filesystem::remove(path);

  EXPECT_DEATH(([&] {
    uv::loop loop;
    uv::pipe_listener listener(loop, path, true);
    uv::tcp tcp_listener(loop);
    tcp_listener.bind(uv::ipv4{"127.0.0.1", 0});
    const uv::ipv4 address{"127.0.0.1", tcp_listener.sockname().port()};
    std::unique_ptr<uv::tcp> peer;
    tcp_listener.listen([&](uv::tcp &server, uv::status status) {
      if (!status) {
        std::terminate();
      }
      peer = std::make_unique<uv::tcp>(loop);
      server.accept(*peer);
    });
    std::optional<uv::pipe_connection> pipe;
    std::optional<uv::tcp_connection> source;
    auto establish = [&]() -> uv::co::task<void> {
      pipe.emplace(co_await uv::pipe_connection::connect(path, true));
      source.emplace(co_await uv::tcp_connection::connect(address));
      loop.stop();
    };
    auto setup = uv::co::spawn(loop, establish());
    loop.run();
    setup.rethrow_if_failed();
    auto writer = [&]() -> uv::co::task<void> {
      co_await pipe->write_with_handle("x", *source);
    };
    auto execution = uv::co::spawn(loop, writer());
    (void)execution;
    source.reset();
  }()), "");

  std::filesystem::remove(path);
}
