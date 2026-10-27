#include "co-test-support.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include "uvpp/co/resource_scope.hpp"
#include "uvpp/co/sleep.hpp"
#include "uvpp/co/task_scope.hpp"
#include "uvpp/net/pipe_listener.hpp"
#include "uvpp/net/tcp_listener.hpp"

using namespace std::chrono_literals;
using namespace uvpp::test;

TEST(UvppV3Coroutine, internalTcpListenerCloseCompletionJoinsAndChecksAffinity) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  uv::loop loop;
  uv::tcp_listener listener(loop, uv::ipv4{"127.0.0.1", 0});
  uv::loop other_loop;
  bool wrong_loop_rejected = false;
  bool first_resumed = false;
  bool second_resumed = false;
  bool first_initiated = false;
  bool second_initiated = false;

  auto wrong_loop = [&]() -> uv::co::task<void> {
    try {
      co_await uv::detail::close_completion(listener);
    } catch (const std::logic_error &) {
      wrong_loop_rejected = true;
    }
  };
  auto first = [&]() -> uv::co::task<void> {
    auto close = uv::detail::close_completion(listener);
    co_await close;
    first_initiated = close.initiated_close();
    first_resumed = true;
    loop.stop();
  };
  auto second = [&]() -> uv::co::task<void> {
    auto close = uv::detail::close_completion(listener);
    co_await close;
    second_initiated = close.initiated_close();
    second_resumed = true;
  };

  auto wrong_execution = uv::co::spawn(other_loop, wrong_loop());
  EXPECT_TRUE(wrong_execution.done());
  EXPECT_NO_THROW(wrong_execution.rethrow_if_failed());
  EXPECT_TRUE(wrong_loop_rejected);
  other_loop.close();

  auto first_execution = uv::co::spawn(loop, first());
  auto second_execution = uv::co::spawn(loop, second());
  loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_resumed);
  EXPECT_TRUE(second_resumed);
  EXPECT_NE(first_initiated, second_initiated);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, publicTcpListenerCloseQuiescesAnActiveAccept) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  uv::loop loop;
  uv::tcp_listener listener(loop, uv::ipv4{"127.0.0.1", 0});
  uv::co::task_scope tasks(loop);
  bool accept_canceled = false;
  bool close_completed = false;

  auto accepter = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener.accept();
    } catch (const uv::error &error) {
      accept_canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto closer = [&]() -> uv::co::task<void> {
    tasks.spawn(accepter());
    co_await listener.close();
    close_completed = true;
    co_await tasks.join();
    loop.stop();
  };

  auto execution = uv::co::spawn(loop, closer());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(close_completed);
  EXPECT_TRUE(accept_canceled);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, publicPipeListenerCloseQuiescesAnActiveAccept) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }

  const auto path = v3_pipe_path() + "-public-close";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path);
  uv::co::task_scope tasks(loop);
  bool accept_canceled = false;
  bool close_completed = false;

  auto accepter = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener.accept();
    } catch (const uv::error &error) {
      accept_canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto closer = [&]() -> uv::co::task<void> {
    tasks.spawn(accepter());
    co_await listener.close();
    close_completed = true;
    co_await tasks.join();
    loop.stop();
  };

  auto execution = uv::co::spawn(loop, closer());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(close_completed);
  EXPECT_TRUE(accept_canceled);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, tcpListenerLateNotificationCannotReachQuiescedAccept) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  uv::loop loop;
  uv::tcp_listener listener(loop, uv::ipv4{"127.0.0.1", 0});
  uv::co::task_scope tasks(loop);
  int accept_deliveries = 0;
  bool accept_canceled = false;
  bool listener_close_completed = false;

  auto accepter = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener.accept();
    } catch (const uv::error &error) {
      ++accept_deliveries;
      accept_canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto parent = [&]() -> uv::co::task<void> {
    tasks.spawn(accepter());
    co_await uv::detail::close_completion(listener);
    listener_close_completed = true;
    co_await tasks.join();

    // Model a stale notification after native close. The owner deliberately
    // remains alive so this only exercises terminal slot handling; no libuv
    // function is called. It must not reach the completed accept frame.
    uv::detail::tcp_listener_state::on_connection(
        listener.native_stream(), 0);
    loop.stop();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(listener_close_completed);
  EXPECT_TRUE(accept_canceled);
  EXPECT_EQ(accept_deliveries, 1);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, pipeListenerAcceptsIntoAnIndependentMovableConnectionOwner) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-listener-move";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path);
  bool accepted = false;
  bool accepted_owner_was_stable = false;
  bool client_connected = false;

  auto server = [&]() -> uv::co::task<void> {
    auto connection = co_await listener.accept();
    auto *before_move = connection.native();
    auto moved = std::move(connection);
    accepted_owner_was_stable = moved.native() == before_move;
    accepted = true;
    listener.request_close();
  };
  auto client = [&]() -> uv::co::task<void> {
    auto connection = co_await uv::pipe_connection::connect(path);
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
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, pipeListenerRejectsSecondConcurrentAccept) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-listener-busy";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path);
  bool first_accepted = false;
  int second_status = 0;
  auto first = [&]() -> uv::co::task<void> {
    auto connection = co_await listener.accept();
    first_accepted = connection.native() != nullptr;
    listener.request_close();
  };
  auto second = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener.accept();
    } catch (const uv::error &error) {
      second_status = error.code().value();
    }
  };
  auto client = [&]() -> uv::co::task<void> {
    auto connection = co_await uv::pipe_connection::connect(path);
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
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, pipeListenerRejectsAcceptFromAnotherLoop) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-listener-affinity";
  std::filesystem::remove(path);
  uv::loop listener_loop;
  uv::pipe_listener listener(listener_loop, path);
  uv::loop other_loop;
  bool rejected = false;
  auto cross_loop_accept = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener.accept();
    } catch (const std::logic_error &) {
      rejected = true;
    }
  };

  auto execution = uv::co::spawn(other_loop, cross_loop_accept());
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(rejected);

  other_loop.close();
  listener.request_close();
  listener_loop.run();
  EXPECT_NO_THROW(listener_loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, taskScopeStopCancelsPipeAcceptBeforeResumption) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-listener-cancel";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path);
  uv::co::task_scope scope(loop);
  bool canceled = false;
  auto accepter = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener.accept();
    } catch (const uv::error &error) {
      canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(accepter());
    scope.request_stop();
    co_await scope.join();
    listener.request_close();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(canceled);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, resourceScopeQuiescesPendingPipeAcceptBeforeListenerClose) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-listener-scope-close";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  int deliveries = 0;
  bool canceled = false;
  bool cleanup_completed = false;
  auto parent = [&]() -> uv::co::task<void> {
    auto listener = resources.own(uv::pipe_listener{loop, path});
    auto accept = [&]() -> uv::co::task<void> {
      try {
        (void)co_await listener.accept();
      } catch (const uv::error &error) {
        ++deliveries;
        canceled = error.code().value() == UV_ECANCELED;
      }
    };
    tasks.spawn(accept());
    co_await resources.finish();
    cleanup_completed = true;
    co_await tasks.join();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(deliveries, 1);
  EXPECT_TRUE(canceled);
  EXPECT_TRUE(cleanup_completed);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
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
  const auto address = listener->local_address().to_v4();
  bool first_accepted = false;
  int second_status = 0;
  auto first = [&]() -> uv::co::task<void> {
    auto connection = co_await listener->accept();
    first_accepted = connection.native() != nullptr;
    listener->request_close();
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
  listener->request_close();
  listener_loop.run();
  EXPECT_NO_THROW(listener_loop.close());
}

TEST(UvppV3Coroutine, taskScopeStopCancelsAcceptBeforeResumption) {
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

  uv::co::task_scope scope(loop);
  bool canceled = false;
  auto accepter = [&]() -> uv::co::task<void> {
    try {
      (void)co_await listener->accept();
    } catch (const uv::error &error) {
      canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(accepter());
    scope.request_stop();
    co_await scope.join();
    listener->request_close();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(canceled);
  EXPECT_NO_THROW(loop.close());
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
  const auto address = listener->local_address().to_v4();
  bool accepted = false;
  bool saw_eof = false;
  auto server = [&]() -> uv::co::task<void> {
    auto connection = co_await listener->accept();
    accepted = connection.native() != nullptr;
    std::array<std::byte, 8> buffer{};
    saw_eof = (co_await connection.read_some(buffer)).eof();
    listener->request_close();
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

TEST(UvppV3Coroutine, taskScopeOwnsConcurrentAcceptedConnectionHandlers) {
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
  const auto address = listener->local_address().to_v4();
  uv::co::task_scope scope(loop);
  int handlers_started = 0;
  int handlers_completed = 0;

  auto handle = [&](uv::tcp_connection connection) -> uv::co::task<void> {
    EXPECT_NE(connection.native(), nullptr);
    ++handlers_started;
    co_await uv::co::sleep_for(1ms);
    ++handlers_completed;
  };
  auto server = [&]() -> uv::co::task<void> {
    for (int count = 0; count != 2; ++count) {
      auto connection = co_await listener->accept();
      scope.spawn(handle(std::move(connection)));
    }
    co_await scope.join();
    listener->request_close();
  };
  auto client = [&]() -> uv::co::task<void> {
    auto first = co_await uv::tcp_connection::connect(address);
    auto second = co_await uv::tcp_connection::connect(address);
    (void)first;
    (void)second;
  };

  auto server_execution = uv::co::spawn(loop, server());
  auto client_execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(server_execution.rethrow_if_failed());
  EXPECT_NO_THROW(client_execution.rethrow_if_failed());
  EXPECT_EQ(handlers_started, 2);
  EXPECT_EQ(handlers_completed, 2);
  EXPECT_NO_THROW(loop.close());
}
