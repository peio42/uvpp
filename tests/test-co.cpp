#include <array>
#include <chrono>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "uvpp/co/sleep.hpp"
#include "uvpp/handles/timer.hpp"
#include "uvpp/handles/tcp.hpp"
#include "uvpp/net/tcp_connection.hpp"

using namespace std::chrono_literals;

TEST(UvppV3Coroutine, sleepForBindsColdTaskToTheSpawnLoop) {
  uv::loop loop;
  bool entered = false;
  bool resumed = false;
  bool callback_ran = false;

  auto sleeper = [&]() -> uv::co::task<void> {
    entered = true;
    co_await uv::co::sleep_for(1ms);
    resumed = true;
  };

  auto root = sleeper();
  EXPECT_FALSE(entered);

  uv::timer callback_timer(loop);
  callback_timer.start(1ms, [&](uv::timer &self) {
    callback_ran = true;
    self.close();
  });

  auto execution = uv::co::spawn(loop, std::move(root));
  EXPECT_TRUE(entered);
  EXPECT_FALSE(resumed);

  loop.run();

  EXPECT_TRUE(callback_ran);
  EXPECT_TRUE(resumed);
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  loop.close();
}

TEST(UvppV3Coroutine, sleepForDeliversTaskFailureToTheSpawnHandle) {
  uv::loop loop;

  auto failing_sleeper = []() -> uv::co::task<void> {
    co_await uv::co::sleep_for(0ms);
    throw std::runtime_error{"expected task failure"};
  };

  auto execution = uv::co::spawn(loop, failing_sleeper());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_THROW(execution.rethrow_if_failed(), std::runtime_error);
  loop.close();
}

TEST(UvppV3Coroutine, childTaskInheritsItsParentsLoopAndReturnsItsValue) {
  uv::loop loop;
  int result = 0;

  auto child = []() -> uv::co::task<int> {
    co_await uv::co::sleep_for(1ms);
    co_return 42;
  };
  auto parent = [&]() -> uv::co::task<void> {
    result = co_await child();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(result, 42);
  loop.close();
}

TEST(UvppV3Coroutine, voidChildTaskResumesItsParent) {
  uv::loop loop;
  bool child_completed = false;
  bool parent_resumed = false;

  auto child = [&]() -> uv::co::task<void> {
    co_await uv::co::sleep_for(0ms);
    child_completed = true;
  };
  auto parent = [&]() -> uv::co::task<void> {
    co_await child();
    parent_resumed = true;
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(child_completed);
  EXPECT_TRUE(parent_resumed);
  loop.close();
}

TEST(UvppV3Coroutine, childTaskMovesItsValueToTheParent) {
  uv::loop loop;
  std::unique_ptr<int> result;

  auto child = []() -> uv::co::task<std::unique_ptr<int>> {
    co_await uv::co::sleep_for(0ms);
    co_return std::make_unique<int>(42);
  };
  auto parent = [&]() -> uv::co::task<void> {
    result = co_await child();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, 42);
  loop.close();
}

TEST(UvppV3Coroutine, childTaskFailurePropagatesToItsParent) {
  uv::loop loop;
  bool parent_resumed = false;

  auto child = []() -> uv::co::task<int> {
    co_await uv::co::sleep_for(0ms);
    throw std::runtime_error{"expected child failure"};
  };
  auto parent = [&]() -> uv::co::task<void> {
    (void)co_await child();
    parent_resumed = true;
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_FALSE(parent_resumed);
  EXPECT_THROW(execution.rethrow_if_failed(), std::runtime_error);
  loop.close();
}

TEST(UvppV3Coroutine, tcpConnectionConnectsMovesAndCloses) {
  uv::loop loop;
  uv::tcp listener(loop);
  try {
    listener.bind(uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      listener.close();
      loop.run();
      loop.close();
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }
  const auto address = uv::ipv4{"127.0.0.1", listener.sockname().port()};
  listener.listen([&](uv::tcp &, uv::result status) {
    EXPECT_TRUE(status);
  });

  bool connected = false;
  bool write_completed = false;
  bool submission_failure_delivered = false;
  auto client = [&]() -> uv::co::task<void> {
    auto socket = co_await uv::tcp_connection::connect(address);
    auto *before_move = socket.native_handle();
    auto moved = std::move(socket);
    EXPECT_EQ(moved.native_handle(), before_move);
    try {
      co_await socket.write("submission failure");
    } catch (const uv::error &error) {
      submission_failure_delivered = error.code().value() == UV_EBADF;
    }
    std::string data{"borrowed request"};
    co_await moved.write(data);
    data[0] = 'B'; // The await has completed; the borrowed bytes are reusable.
    write_completed = true;
    listener.close();
    connected = true;
  };

  auto execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_TRUE(connected);
  EXPECT_TRUE(submission_failure_delivered);
  EXPECT_TRUE(write_completed);
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, tcpConnectionRefusalClosesBeforeDeliveringTheError) {
  uv::loop loop;
  uv::tcp listener(loop);
  try {
    listener.bind(uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      listener.close();
      loop.run();
      loop.close();
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }
  const auto address = uv::ipv4{"127.0.0.1", listener.sockname().port()};
  listener.close();
  loop.run();

  int status = 0;
  auto client = [&]() -> uv::co::task<void> {
    try {
      auto socket = co_await uv::tcp_connection::connect(address);
      (void)socket;
    } catch (const uv::error &error) {
      status = error.code().value();
    }
  };

  auto execution = uv::co::spawn(loop, client());
  loop.run();

  if (status == UV_EPERM) {
    loop.close();
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }
  EXPECT_EQ(status, UV_ECONNREFUSED);
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, tcpConnectionClosesDuringExceptionalTaskExit) {
  uv::loop loop;
  uv::tcp listener(loop);
  try {
    listener.bind(uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      listener.close();
      loop.run();
      loop.close();
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }
  const auto address = uv::ipv4{"127.0.0.1", listener.sockname().port()};
  listener.listen([&](uv::tcp &server, uv::result) { server.close(); });

  auto client = [&]() -> uv::co::task<void> {
    auto socket = co_await uv::tcp_connection::connect(address);
    throw std::runtime_error{"after connect"};
  };

  auto execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_THROW(execution.rethrow_if_failed(), std::runtime_error);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, tcpConnectionRejectsInvalidAddressDuringSetup) {
  EXPECT_THROW((uv::ipv4{"not-an-address", 80}), uv::error);
}

TEST(UvppV3Coroutine, tcpConnectionReadSomeStopsAndReleasesSlotsBeforeResumption) {
  uv::loop loop;
  uv::tcp listener(loop);
  try {
    listener.bind(uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      listener.close();
      loop.run();
      loop.close();
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }
  const auto address = uv::ipv4{"127.0.0.1", listener.sockname().port()};
  std::unique_ptr<uv::tcp> peer;
  std::unique_ptr<uv::write_request> reply_request;
  std::string reply{"reply"};
  listener.listen([&](uv::tcp &server, uv::result status) {
    ASSERT_TRUE(status);
    peer = std::make_unique<uv::tcp>(loop);
    ASSERT_NO_THROW(server.accept(*peer));
    reply_request = std::make_unique<uv::write_request>();
    peer->write(*reply_request, std::as_bytes(std::span{reply.data(), reply.size()}),
      [&](uv::write_request &, uv::result write_status) {
        EXPECT_TRUE(write_status);
        peer->close();
        server.close();
      });
  });

  std::string received;
  bool saw_eof = false;
  auto client = [&]() -> uv::co::task<void> {
    auto socket = co_await uv::tcp_connection::connect(address);
    std::array<std::byte, 64> buffer{};
    auto first = co_await socket.read_some(buffer);
    EXPECT_FALSE(first.eof());
    received.assign(reinterpret_cast<const char *>(buffer.data()), first.count());
    auto second = co_await socket.read_some(buffer);
    saw_eof = second.eof();
  };

  auto execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(received, reply);
  EXPECT_TRUE(saw_eof);
  EXPECT_NO_THROW(loop.close());
}
