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
#include "uvpp/net/tcp_listener.hpp"

using namespace std::chrono_literals;
using namespace uvpp::test;

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
  listener.listen([&](uv::tcp &, uv::status status) {
    EXPECT_TRUE(status);
  });

  bool connected = false;
  bool write_completed = false;
  bool submission_failure_delivered = false;
  auto client = [&]() -> uv::co::task<void> {
    auto socket = co_await uv::tcp_connection::connect(address);
    auto *before_move = socket.native();
    auto moved = std::move(socket);
    EXPECT_EQ(moved.native(), before_move);
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

TEST(UvppV3Coroutine, internalTcpCloseCompletionJoinsAndReleasesBeforeResumption) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::loop other_loop;
  bool wrong_loop_rejected = false;
  bool first_resumed = false;
  bool second_resumed = false;
  bool post_close_joined = false;
  bool first_initiated = false;
  bool second_initiated = false;

  auto wrong_loop = [&]() -> uv::co::task<void> {
    try {
      co_await uv::detail::close_completion(*pair.client);
    } catch (const std::logic_error &) {
      wrong_loop_rejected = true;
    }
  };
  auto first = [&]() -> uv::co::task<void> {
    auto close = uv::detail::close_completion(*pair.client);
    co_await close;
    first_initiated = close.initiated_close();
    first_resumed = true;

    auto after_close = uv::detail::close_completion(*pair.client);
    co_await after_close;
    post_close_joined = !after_close.initiated_close();
    pair.loop.stop();
  };
  auto second = [&]() -> uv::co::task<void> {
    auto close = uv::detail::close_completion(*pair.client);
    co_await close;
    second_initiated = close.initiated_close();
    second_resumed = true;
  };

  auto wrong_execution = uv::co::spawn(other_loop, wrong_loop());
  EXPECT_TRUE(wrong_execution.done());
  EXPECT_NO_THROW(wrong_execution.rethrow_if_failed());
  EXPECT_TRUE(wrong_loop_rejected);
  other_loop.close();

  auto first_execution = uv::co::spawn(pair.loop, first());
  auto second_execution = uv::co::spawn(pair.loop, second());
  pair.loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_resumed);
  EXPECT_TRUE(second_resumed);
  EXPECT_TRUE(post_close_joined);
  EXPECT_NE(first_initiated, second_initiated);
  pair.close();
}

TEST(UvppV3Coroutine, publicTcpCloseIsColdJoinsAndHasAnExplicitResultSurface) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  auto close = pair.client->close();
  EXPECT_FALSE(pair.client->closing());

  bool first_resumed = false;
  bool second_resumed = false;
  bool result_ok = false;
  auto first = [&]() -> uv::co::task<void> {
    co_await close;
    first_resumed = true;
    const auto result = co_await uv::ops::close(*pair.client);
    result_ok = result.has_value();
    pair.loop.stop();
  };
  auto second = [&]() -> uv::co::task<void> {
    co_await pair.client->close();
    second_resumed = true;
  };

  auto first_execution = uv::co::spawn(pair.loop, first());
  auto second_execution = uv::co::spawn(pair.loop, second());
  pair.loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_resumed);
  EXPECT_TRUE(second_resumed);
  EXPECT_TRUE(result_ok);
  pair.close();
}

TEST(UvppV3Coroutine, publicTcpCloseCompletesWhenStopWasAlreadyRequested) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::co::task_scope scope(pair.loop);
  bool stop_observed_before_close = false;
  bool native_close_callback_delivered = false;
  bool close_completed = false;
  bool close_completed_after_native_callback = false;
  bool close_started = false;

  auto closer = [&]() -> uv::co::task<void> {
    stop_observed_before_close = co_await uv::co::stop_requested();
    co_await pair.client->close();
    close_completed_after_native_callback = native_close_callback_delivered;
    close_completed = true;
  };
  auto parent = [&]() -> uv::co::task<void> {
    auto &state = uv::detail::tcp_connection_state::from_handle(pair.client->native_handle());
    state.close_completion_destination = &native_close_callback_delivered;
    scope.request_stop();
    scope.spawn(closer());
    close_started = pair.client->closing();
    EXPECT_FALSE(close_completed);
    co_await scope.join();
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(stop_observed_before_close);
  EXPECT_TRUE(close_started);
  EXPECT_TRUE(native_close_callback_delivered);
  EXPECT_TRUE(close_completed);
  EXPECT_TRUE(close_completed_after_native_callback);
  pair.close();
}

TEST(UvppV3Coroutine, publicTcpCloseWaitsForNativeCallbackAfterStop) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::co::task_scope scope(pair.loop);
  bool native_close_callback_delivered = false;
  bool close_completed = false;
  bool close_completed_after_native_callback = false;
  bool stop_observed_after_close = false;
  bool close_started = false;
  bool close_still_pending_after_stop = false;

  auto closer = [&]() -> uv::co::task<void> {
    co_await pair.client->close();
    close_completed_after_native_callback = native_close_callback_delivered;
    close_completed = true;
    stop_observed_after_close = co_await uv::co::stop_requested();
  };
  auto parent = [&]() -> uv::co::task<void> {
    auto &state = uv::detail::tcp_connection_state::from_handle(pair.client->native_handle());
    state.close_completion_destination = &native_close_callback_delivered;
    scope.spawn(closer());
    close_started = pair.client->closing();
    scope.request_stop();
    close_still_pending_after_stop = !close_completed;
    co_await scope.join();
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(close_started);
  EXPECT_TRUE(close_still_pending_after_stop);
  EXPECT_TRUE(native_close_callback_delivered);
  EXPECT_TRUE(close_completed);
  EXPECT_TRUE(close_completed_after_native_callback);
  EXPECT_TRUE(stop_observed_after_close);
  pair.close();
}

TEST(UvppV3Coroutine, publicTcpCloseRejectsActiveReadWithoutChangingIt) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::co::task_scope reads(pair.loop);
  std::array<std::byte, 8> buffer{};
  bool read_canceled = false;
  bool close_rejected = false;
  bool close_completed = false;

  auto reader = [&]() -> uv::co::task<void> {
    try {
      (void)co_await pair.client->read_some(buffer);
    } catch (const uv::error &error) {
      read_canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto closer = [&]() -> uv::co::task<void> {
    reads.spawn(reader());
    try {
      co_await pair.client->close();
    } catch (const uv::error &error) {
      close_rejected = error.code().value() == UV_EBUSY;
    }
    reads.request_stop();
    co_await reads.join();
    co_await pair.client->close();
    close_completed = true;
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, closer());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(close_rejected);
  EXPECT_TRUE(read_canceled);
  EXPECT_TRUE(close_completed);
  pair.close();
}

TEST(UvppV3Coroutine, publicTcpRequestCloseRejectsActiveReadWithoutChangingIt) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::co::task_scope reads(pair.loop);
  std::array<std::byte, 8> buffer{};
  bool read_started = false;
  bool read_completed = false;
  bool read_canceled = false;
  bool request_rejected = false;
  bool operation_still_alive = false;
  bool owner_still_open = false;
  bool close_completed = false;

  auto reader = [&]() -> uv::co::task<void> {
    read_started = true;
    try {
      (void)co_await pair.client->read_some(buffer);
    } catch (const uv::error &error) {
      read_canceled = error.code().value() == UV_ECANCELED;
    }
    read_completed = true;
  };
  auto closer = [&]() -> uv::co::task<void> {
    reads.spawn(reader());
    try {
      pair.client->request_close();
    } catch (const uv::error &error) {
      request_rejected = error.code().value() == UV_EBUSY;
    }
    operation_still_alive = read_started && !read_completed;
    owner_still_open = !pair.client->closing();
    reads.request_stop();
    co_await reads.join();
    co_await pair.client->close();
    close_completed = true;
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, closer());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(request_rejected);
  EXPECT_TRUE(operation_still_alive);
  EXPECT_TRUE(owner_still_open);
  EXPECT_TRUE(read_canceled);
  EXPECT_TRUE(close_completed);
  pair.close();
}

TEST(UvppV3Coroutine, internalTcpCloseCompletionRetainsStateAfterOwnerDestruction) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  bool resumed = false;
  auto closer = [&]() -> uv::co::task<void> {
    co_await uv::detail::close_completion(*pair.client);
    resumed = true;
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, closer());
  pair.client.reset();
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(resumed);
  pair.close();
}

TEST(UvppV3Coroutine, internalTcpCloseCompletionDeliversDetachedWaitersAfterOwnerReset) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  bool first_resumed = false;
  bool second_resumed = false;
  auto first = [&]() -> uv::co::task<void> {
    co_await uv::detail::close_completion(*pair.client);
    first_resumed = true;
    pair.client.reset();
  };
  auto second = [&]() -> uv::co::task<void> {
    co_await uv::detail::close_completion(*pair.client);
    second_resumed = true;
    pair.loop.stop();
  };

  auto first_execution = uv::co::spawn(pair.loop, first());
  auto second_execution = uv::co::spawn(pair.loop, second());
  pair.loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_resumed);
  EXPECT_TRUE(second_resumed);
  pair.close();
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
  listener.listen([&](uv::tcp &server, uv::status) { server.close(); });

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
  listener.listen([&](uv::tcp &server, uv::status status) {
    ASSERT_TRUE(status);
    peer = std::make_unique<uv::tcp>(loop);
    ASSERT_NO_THROW(server.accept(*peer));
    reply_request = std::make_unique<uv::write_request>();
    peer->write(*reply_request, std::as_bytes(std::span{reply.data(), reply.size()}),
      [&](uv::write_request &, uv::status write_status) {
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
  const auto address = listener->local_address().to_v4();
  bool accepted = false;
  bool accepted_owner_was_stable = false;
  bool client_connected = false;

  auto server = [&]() -> uv::co::task<void> {
    auto connection = co_await listener->accept();
    auto *before_move = connection.native();
    auto moved = std::move(connection);
    accepted_owner_was_stable = moved.native() == before_move;
    accepted = true;
    listener->request_close();
  };
  auto client = [&]() -> uv::co::task<void> {
    auto connection = co_await uv::tcp_connection::connect(address);
    client_connected = connection.native() != nullptr;
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

TEST(UvppV3Coroutine, taskScopeStopCancelsReadAndReleasesItsSlots) {
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

  uv::co::task_scope scope(pair.loop);
  std::array<std::byte, 8> first_buffer{};
  std::array<std::byte, 8> second_buffer{};
  bool canceled = false;
  bool second_saw_eof = false;
  auto reader = [&]() -> uv::co::task<void> {
    try {
      (void)co_await pair.client->read_some(first_buffer);
    } catch (const uv::error &error) {
      canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(reader());
    scope.request_stop();
    co_await scope.join();
    pair.peer->close();
    try {
      second_saw_eof = (co_await pair.client->read_some(second_buffer)).eof();
    } catch (...) {
      pair.loop.stop();
      throw;
    }
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(canceled);
  EXPECT_TRUE(second_saw_eof);
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

TEST(UvppV3Coroutine, tcpConnectionPermitsOneReadAndOneWrite) {
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

  std::array<char, 16> peer_buffer{};
  std::array<std::byte, 16> client_buffer{};
  std::string request{"request"};
  std::string response{"response"};
  uv::write_request response_request;
  bool peer_received_request = false;
  bool write_completed = false;
  std::string client_received;

  pair.peer->read_start(
      [&](uv::tcp &, std::size_t) {
        return uv::buffer_view{peer_buffer.data(), peer_buffer.size()};
      },
      [&](uv::tcp &peer, uv::read_result result) {
        ASSERT_TRUE(result);
        EXPECT_EQ(result.count(), request.size());
        EXPECT_EQ((std::string_view{peer_buffer.data(),
                      static_cast<std::size_t>(result.count())}), request);
        peer_received_request = true;
        peer.read_stop();
        peer.write(response_request, std::as_bytes(std::span{response.data(), response.size()}),
            [](uv::write_request &, uv::status status) { EXPECT_TRUE(status); });
      });

  auto reader = [&]() -> uv::co::task<void> {
    const auto result = co_await pair.client->read_some(client_buffer);
    EXPECT_FALSE(result.eof());
    client_received.assign(reinterpret_cast<const char *>(client_buffer.data()), result.count());
    pair.loop.stop();
  };
  auto writer = [&]() -> uv::co::task<void> {
    co_await pair.client->write(request);
    write_completed = true;
  };

  auto read_execution = uv::co::spawn(pair.loop, reader());
  auto write_execution = uv::co::spawn(pair.loop, writer());
  pair.loop.run();

  EXPECT_NO_THROW(read_execution.rethrow_if_failed());
  EXPECT_NO_THROW(write_execution.rethrow_if_failed());
  EXPECT_TRUE(peer_received_request);
  EXPECT_TRUE(write_completed);
  EXPECT_EQ(client_received, response);
  pair.close();
}

TEST(UvppV3Coroutine, taskScopeStopWaitsForSubmittedBorrowedWrite) {
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

  uv::co::task_scope scope(pair.loop);
  std::string data{"borrowed write survives stop"};
  bool write_completed = false;
  bool observed_stop = false;
  auto writer = [&]() -> uv::co::task<void> {
    co_await pair.client->write(data);
    write_completed = true;
    observed_stop = co_await uv::co::stop_requested();
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(writer());
    scope.request_stop();
    co_await scope.join();
    data[0] = 'B';
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(write_completed);
  EXPECT_TRUE(observed_stop);
  EXPECT_EQ(data.front(), 'B');
  pair.close();
}
