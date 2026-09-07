#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "uvpp/co/sleep.hpp"
#include "uvpp/handles/timer.hpp"
#include "uvpp/handles/tcp.hpp"
#include "uvpp/net/tcp_connection.hpp"
#include "uvpp/net/tcp_listener.hpp"

using namespace std::chrono_literals;

namespace {

bool loopback_tcp_is_permitted() {
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

struct connected_tcp_pair {
  uv::loop loop;
  uv::tcp listener{loop};
  std::unique_ptr<uv::tcp> peer;
  std::optional<uv::tcp_connection> client;
  std::optional<uv::co::spawn_handle> connect_execution;

  void connect() {
    listener.bind(uv::ipv4{"127.0.0.1", 0});
    const auto address = uv::ipv4{"127.0.0.1", listener.sockname().port()};
    listener.listen([&](uv::tcp &server, uv::result status) {
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

} // namespace

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

TEST(UvppV3Coroutine, tcpListenerAcceptsIntoAnIndependentMovableConnectionOwner) {
  uv::loop loop;
  std::unique_ptr<uv::tcp_listener> listener;
  try {
    listener = std::make_unique<uv::tcp_listener>(loop, uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      loop.run(); // Drain the constructor's asynchronous setup cleanup.
      loop.close();
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }
  const auto address = listener->sockname().to_v4();
  bool accepted = false;
  bool accepted_owner_was_stable = false;
  bool client_connected = false;

  auto server = [&]() -> uv::co::task<void> {
    auto connection = co_await listener->accept();
    auto *before_move = connection.native_handle();
    auto moved = std::move(connection);
    accepted_owner_was_stable = moved.native_handle() == before_move;
    accepted = true;
    listener->close();
  };
  auto client = [&]() -> uv::co::task<void> {
    auto connection = co_await uv::tcp_connection::connect(address);
    client_connected = connection.native_handle() != nullptr;
  };

  auto server_execution = uv::co::spawn(loop, server());
  auto client_execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(server_execution.rethrow_if_failed());
  EXPECT_NO_THROW(client_execution.rethrow_if_failed());
  EXPECT_TRUE(accepted);
  EXPECT_TRUE(accepted_owner_was_stable);
  EXPECT_TRUE(client_connected);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, tcpConnectionRejectsReadAndWriteFromAnotherLoop) {
  connected_tcp_pair pair;
  try {
    pair.connect();
  } catch (const uv::error &error) {
    pair.close();
    if (error.code().value() == UV_EPERM) {
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }

  uv::loop other_loop;
  bool read_rejected = false;
  bool write_rejected = false;
  std::array<std::byte, 8> buffer{};

  auto cross_loop_read = [&]() -> uv::co::task<void> {
    try {
      (void)co_await pair.client->read_some(buffer);
    } catch (const std::logic_error &) {
      read_rejected = true;
    }
  };
  auto cross_loop_write = [&]() -> uv::co::task<void> {
    try {
      co_await pair.client->write("wrong loop");
    } catch (const std::logic_error &) {
      write_rejected = true;
    }
  };

  auto read_execution = uv::co::spawn(other_loop, cross_loop_read());
  auto write_execution = uv::co::spawn(other_loop, cross_loop_write());

  EXPECT_TRUE(read_execution.done());
  EXPECT_TRUE(write_execution.done());
  EXPECT_NO_THROW(read_execution.rethrow_if_failed());
  EXPECT_NO_THROW(write_execution.rethrow_if_failed());
  EXPECT_TRUE(read_rejected);
  EXPECT_TRUE(write_rejected);

  other_loop.close();
  pair.close();
}

TEST(UvppV3Coroutine, tcpConnectionRejectsSimultaneousReadSome) {
  connected_tcp_pair pair;
  try {
    pair.connect();
  } catch (const uv::error &error) {
    pair.close();
    if (error.code().value() == UV_EPERM) {
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }

  std::array<std::byte, 8> first_buffer{};
  std::array<std::byte, 8> second_buffer{};
  bool first_saw_eof = false;
  int second_status = 0;
  auto first = [&]() -> uv::co::task<void> {
    first_saw_eof = (co_await pair.client->read_some(first_buffer)).eof();
    pair.loop.stop();
  };
  auto second = [&]() -> uv::co::task<void> {
    try {
      (void)co_await pair.client->read_some(second_buffer);
    } catch (const uv::error &error) {
      second_status = error.code().value();
    }
  };

  auto first_execution = uv::co::spawn(pair.loop, first());
  auto second_execution = uv::co::spawn(pair.loop, second());
  pair.peer->close();
  pair.loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_saw_eof);
  EXPECT_EQ(second_status, UV_EBUSY);
  pair.close();
}

TEST(UvppV3Coroutine, tcpConnectionRejectsSimultaneousWrite) {
  connected_tcp_pair pair;
  try {
    pair.connect();
  } catch (const uv::error &error) {
    pair.close();
    if (error.code().value() == UV_EPERM) {
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }

  std::string first_data{"first"};
  std::string second_data{"second"};
  bool first_completed = false;
  int second_status = 0;
  auto first = [&]() -> uv::co::task<void> {
    co_await pair.client->write(first_data);
    first_completed = true;
    pair.loop.stop();
  };
  auto second = [&]() -> uv::co::task<void> {
    try {
      co_await pair.client->write(second_data);
    } catch (const uv::error &error) {
      second_status = error.code().value();
    }
  };

  auto first_execution = uv::co::spawn(pair.loop, first());
  auto second_execution = uv::co::spawn(pair.loop, second());
  pair.loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(second_status, UV_EBUSY);
  pair.close();
}

TEST(UvppV3Coroutine, tcpListenerRejectsSecondConcurrentAccept) {
  uv::loop loop;
  std::unique_ptr<uv::tcp_listener> listener;
  try {
    listener = std::make_unique<uv::tcp_listener>(loop, uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      loop.run();
      loop.close();
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }
  const auto address = listener->sockname().to_v4();
  bool first_accepted = false;
  int second_status = 0;
  auto first = [&]() -> uv::co::task<void> {
    auto connection = co_await listener->accept();
    first_accepted = connection.native_handle() != nullptr;
    listener->close();
  };
  auto second = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener->accept();
    } catch (const uv::error &error) {
      second_status = error.code().value();
    }
  };
  auto client = [&]() -> uv::co::task<void> {
    auto connection = co_await uv::tcp_connection::connect(address);
    (void)connection;
  };

  auto first_execution = uv::co::spawn(loop, first());
  auto second_execution = uv::co::spawn(loop, second());
  auto client_execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_NO_THROW(client_execution.rethrow_if_failed());
  EXPECT_TRUE(first_accepted);
  EXPECT_EQ(second_status, UV_EBUSY);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, tcpListenerRejectsAcceptFromAnotherLoop) {
  uv::loop listener_loop;
  std::unique_ptr<uv::tcp_listener> listener;
  try {
    listener = std::make_unique<uv::tcp_listener>(listener_loop, uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      listener_loop.run();
      listener_loop.close();
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }

  uv::loop other_loop;
  bool rejected = false;
  auto cross_loop_accept = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener->accept();
    } catch (const std::logic_error &) {
      rejected = true;
    }
  };

  auto execution = uv::co::spawn(other_loop, cross_loop_accept());
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(rejected);

  other_loop.close();
  listener->close();
  listener_loop.run();
  EXPECT_NO_THROW(listener_loop.close());
}

TEST(UvppV3Coroutine, tcpListenerAcceptsPeerThatImmediatelyCloses) {
  uv::loop loop;
  std::unique_ptr<uv::tcp_listener> listener;
  try {
    listener = std::make_unique<uv::tcp_listener>(loop, uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      loop.run();
      loop.close();
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }
  const auto address = listener->sockname().to_v4();
  bool accepted = false;
  bool saw_eof = false;
  auto server = [&]() -> uv::co::task<void> {
    auto connection = co_await listener->accept();
    accepted = connection.native_handle() != nullptr;
    std::array<std::byte, 8> buffer{};
    saw_eof = (co_await connection.read_some(buffer)).eof();
    listener->close();
  };
  auto client = [&]() -> uv::co::task<void> {
    auto connection = co_await uv::tcp_connection::connect(address);
    (void)connection; // Destruction immediately starts close.
  };

  auto server_execution = uv::co::spawn(loop, server());
  auto client_execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(server_execution.rethrow_if_failed());
  EXPECT_NO_THROW(client_execution.rethrow_if_failed());
  EXPECT_TRUE(accepted);
  EXPECT_TRUE(saw_eof);
  EXPECT_NO_THROW(loop.close());
}

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

TEST(UvppV3CoroutineDeathTest, tcpListenerCloseWithActiveAcceptTerminates) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  EXPECT_DEATH(([&] {
    uv::loop loop;
    uv::tcp_listener listener(loop, uv::ipv4{"127.0.0.1", 0});
    auto waiter = [&]() -> uv::co::task<void> {
      (void)co_await listener.accept();
    };
    auto execution = uv::co::spawn(loop, waiter());
    listener.close();
  }()), "");
}

TEST(UvppV3CoroutineDeathTest, tcpListenerDestructionWithActiveAcceptTerminates) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  EXPECT_DEATH(([&] {
    uv::loop loop;
    {
      uv::tcp_listener listener(loop, uv::ipv4{"127.0.0.1", 0});
      auto waiter = [&]() -> uv::co::task<void> {
        (void)co_await listener.accept();
      };
      auto execution = uv::co::spawn(loop, waiter());
    }
  }()), "");
}
