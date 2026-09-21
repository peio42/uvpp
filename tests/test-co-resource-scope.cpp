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
#include "uvpp/net/udp_socket.hpp"

using namespace std::chrono_literals;
using namespace uvpp::test;

TEST(UvppV3Coroutine, resourceScopeOwnsTcpUntilFinishThenInvalidatesViews) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::co::task_scope tasks(pair.loop);
  uv::co::resource_scope resources(pair.loop);
  bool handler_received_only_a_view = false;
  bool handler_joined = false;
  bool finish_completed = false;
  bool late_view_rejected = false;

  auto handler = [&](uv::tcp_connection_view) -> uv::co::task<void> {
    handler_received_only_a_view = true;
    co_return;
  };
  auto parent = [&]() -> uv::co::task<void> {
    auto connection = resources.own(std::move(*pair.client));
    pair.client.reset();
    auto view = connection.view();
    tasks.spawn(handler(view));
    co_await tasks.join();
    handler_joined = true;
    co_await resources.finish();
    finish_completed = true;
    try {
      (void)view.write("late view");
    } catch (const std::logic_error &) {
      late_view_rejected = true;
    }
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(handler_received_only_a_view);
  EXPECT_TRUE(handler_joined);
  EXPECT_TRUE(finish_completed);
  EXPECT_TRUE(late_view_rejected);
  pair.close();
}

TEST(UvppV3Coroutine, resourceScopeWaitsForEveryTcpCloseCompletion) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  uv::loop loop;
  uv::tcp listener(loop);
  std::vector<std::unique_ptr<uv::tcp>> peers;
  try {
    listener.bind(uv::ipv4{"127.0.0.1", 0});
  } catch (const uv::error &error) {
    listener.close();
    loop.run();
    loop.close();
    if (error.code().value() == UV_EPERM) {
      GTEST_SKIP() << "loopback TCP is not permitted in this environment";
    }
    throw;
  }
  listener.listen([&](uv::tcp &server, uv::status status) {
    EXPECT_TRUE(status);
    auto peer = std::make_unique<uv::tcp>(loop);
    EXPECT_NO_THROW(server.accept(*peer));
    peers.push_back(std::move(peer));
  });
  const uv::ipv4 address{"127.0.0.1", listener.sockname().port()};
  bool finish_completed = false;

  auto parent = [&]() -> uv::co::task<void> {
    uv::co::resource_scope resources(loop);
    auto first = resources.own(co_await uv::tcp_connection::connect(address));
    auto second = resources.own(co_await uv::tcp_connection::connect(address));
    (void)first;
    (void)second;
    co_await resources.finish();
    finish_completed = true;
    listener.close();
    loop.stop();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(finish_completed);
  EXPECT_EQ(peers.size(), 2U);
  for (auto &peer : peers) {
    if (!peer->closing()) {
      peer->close();
    }
  }
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, resourceScopeStressClosesManyConnectionsAndQuiescesPendingAccept) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  constexpr std::size_t connection_count = 16;
  constexpr int round_count = 8;
  for (int round = 0; round != round_count; ++round) {
    SCOPED_TRACE(round);
    uv::loop loop;
    uv::co::task_scope tasks(loop);
    uv::co::resource_scope resources(loop);
    uv::tcp_listener raw_listener(loop, uv::ipv4{"127.0.0.1", 0});
    const auto address = raw_listener.local_address().to_v4();
    std::size_t accepted_connections = 0;
    std::size_t completed_handlers = 0;
    std::size_t pending_accept_deliveries = 0;
    bool pending_accept_canceled = false;
    bool cleanup_completed = false;
    bool clients_connected = false;

    auto handler = [&](uv::tcp_connection_view) -> uv::co::task<void> {
      ++completed_handlers;
      co_return;
    };
    auto server = [&]() -> uv::co::task<void> {
      auto listener = resources.own(std::move(raw_listener));
      for (std::size_t index = 0; index != connection_count; ++index) {
        auto connection = resources.own(co_await listener.accept());
        ++accepted_connections;
        tasks.spawn(handler(connection.view()));
      }
      auto pending_accept = [&]() -> uv::co::task<void> {
        try {
          (void)co_await listener.accept();
        } catch (const uv::error &error) {
          ++pending_accept_deliveries;
          pending_accept_canceled = error.code().value() == UV_ECANCELED;
        }
      };
      tasks.spawn(pending_accept());

      // Connection handlers have already completed their I/O-free work. The
      // final accept remains armed specifically to exercise scope quiescing
      // before dependent connection close completion.
      co_await resources.finish();
      cleanup_completed = true;
      co_await tasks.join();
    };
    auto clients = [&]() -> uv::co::task<void> {
      std::vector<uv::tcp_connection> connections;
      connections.reserve(connection_count);
      for (std::size_t index = 0; index != connection_count; ++index) {
        connections.push_back(co_await uv::tcp_connection::connect(address));
      }
      clients_connected = true;
    };

    auto server_execution = uv::co::spawn(loop, server());
    auto client_execution = uv::co::spawn(loop, clients());
    loop.run();

    EXPECT_NO_THROW(server_execution.rethrow_if_failed());
    EXPECT_NO_THROW(client_execution.rethrow_if_failed());
    EXPECT_TRUE(clients_connected);
    EXPECT_EQ(accepted_connections, connection_count);
    EXPECT_EQ(completed_handlers, connection_count);
    EXPECT_TRUE(cleanup_completed);
    EXPECT_TRUE(pending_accept_canceled);
    EXPECT_EQ(pending_accept_deliveries, 1U);
    EXPECT_NO_THROW(loop.close());
  }
}

TEST(UvppV3Coroutine, resourceScopeRetriesPartialCleanup) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }

  for (bool with_primary_failure : {false, true}) {
    uv::loop loop;
    uv::co::resource_scope resources(loop);
    auto first = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    auto busy = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    auto last = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    std::array<std::byte, 32> buffer{};
    bool canceled = false;
    bool recovered = false;
    auto receive = [&]() -> uv::co::task<void> {
      try {
        (void)co_await busy.view().recv_from(buffer);
      } catch (const uv::error &error) {
        canceled = error.code().value() == UV_ECANCELED;
      }
    };
    uv::co::task_scope readers(loop);
    readers.spawn(receive());
    auto parent = [&]() -> uv::co::task<void> {
      std::exception_ptr primary;
      if (with_primary_failure) {
        try {
          throw std::runtime_error{"primary failure"};
        } catch (...) {
          primary = std::current_exception();
        }
      }
      bool rejected = false;
      try {
        co_await resources.finish();
      } catch (const std::logic_error &) {
        rejected = true;
      }
      EXPECT_TRUE(rejected);
      EXPECT_THROW((void)first.view().local_address(), std::logic_error);
      EXPECT_NO_THROW((void)busy.view().local_address());
      EXPECT_NO_THROW((void)last.view().local_address());
      uv::udp_socket extra{loop, uv::ipv4{"127.0.0.1", 0}};
      EXPECT_THROW((void)resources.own(std::move(extra)), std::logic_error);
      co_await extra.close();
      readers.request_stop();
      co_await readers.join();
      co_await resources.finish();
      co_await resources.finish();
      EXPECT_THROW((void)busy.view().local_address(), std::logic_error);
      EXPECT_THROW((void)last.view().local_address(), std::logic_error);
      recovered = true;
      if (primary) {
        std::rethrow_exception(primary);
      }
    };
    auto execution = uv::co::spawn(loop, parent());
    loop.run();
    EXPECT_TRUE(recovered);
    EXPECT_TRUE(canceled);
    if (with_primary_failure) {
      EXPECT_THROW(execution.rethrow_if_failed(), std::runtime_error);
    } else {
      EXPECT_NO_THROW(execution.rethrow_if_failed());
    }
    EXPECT_NO_THROW(loop.close());
  }
}

TEST(UvppV3Coroutine, resourceScopeJoinsAnExternallyStartedCloseBeforeRetry) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }

  uv::loop loop;
  uv::co::resource_scope resources(loop);
  auto first = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
  uv::udp_socket externally_closing{loop, uv::ipv4{"127.0.0.1", 0}};
  externally_closing.request_close();
  auto joined = resources.own(std::move(externally_closing));
  auto busy = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
  std::array<std::byte, 32> buffer{};
  bool receive_canceled = false;
  bool first_attempt_rejected = false;
  bool retry_completed = false;

  auto receive = [&]() -> uv::co::task<void> {
    try {
      (void)co_await busy.view().recv_from(buffer);
    } catch (const uv::error &error) {
      receive_canceled = error.code().value() == UV_ECANCELED;
    }
  };
  uv::co::task_scope readers(loop);
  readers.spawn(receive());

  auto parent = [&]() -> uv::co::task<void> {
    try {
      co_await resources.finish();
    } catch (const std::logic_error &) {
      first_attempt_rejected = true;
    }
    EXPECT_THROW((void)first.view().local_address(), std::logic_error);
    EXPECT_THROW((void)joined.view().local_address(), std::logic_error);
    EXPECT_NO_THROW((void)busy.view().local_address());

    readers.request_stop();
    co_await readers.join();
    co_await resources.finish();
    EXPECT_THROW((void)busy.view().local_address(), std::logic_error);
    retry_completed = true;
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(first_attempt_rejected);
  EXPECT_TRUE(receive_canceled);
  EXPECT_TRUE(retry_completed);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, resourceScopeFinishIsColdAndRejectsConcurrentAndWrongLoopUse) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }
  uv::loop loop;
  uv::loop other_loop;
  uv::co::resource_scope resources(loop);
  { auto abandoned = resources.finish(); }
  auto wrong = uv::co::spawn(other_loop, resources.finish());
  EXPECT_THROW(wrong.rethrow_if_failed(), std::logic_error);
  (void)resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
  auto first = uv::co::spawn(loop, resources.finish());
  EXPECT_FALSE(first.done());
  auto concurrent = uv::co::spawn(loop, resources.finish());
  EXPECT_THROW(concurrent.rethrow_if_failed(), std::logic_error);
  loop.run();
  EXPECT_NO_THROW(first.rethrow_if_failed());
  auto again = uv::co::spawn(loop, resources.finish());
  EXPECT_TRUE(again.done());
  EXPECT_NO_THROW(again.rethrow_if_failed());
  auto wrong_after_success = uv::co::spawn(other_loop, resources.finish());
  EXPECT_THROW(wrong_after_success.rethrow_if_failed(), std::logic_error);
  EXPECT_NO_THROW(loop.close());
  EXPECT_NO_THROW(other_loop.close());
}

TEST(UvppV3Coroutine, resourceScopeClosesAfterFailFastTaskJoin) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::co::task_scope tasks(pair.loop);
  uv::co::resource_scope resources(pair.loop);
  bool sibling_canceled = false;
  bool primary_failure_observed = false;
  bool cleanup_completed = false;

  auto failing = [](uv::tcp_connection_view) -> uv::co::task<void> {
    co_await uv::co::sleep_for(0ms);
    throw std::runtime_error{"expected handler failure"};
  };
  auto sibling = [&](uv::tcp_connection_view) -> uv::co::task<void> {
    try {
      co_await uv::co::sleep_for(1h);
    } catch (const uv::error &error) {
      sibling_canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto parent = [&]() -> uv::co::task<void> {
    auto connection = resources.own(std::move(*pair.client));
    pair.client.reset();
    tasks.spawn(failing(connection.view()));
    tasks.spawn(sibling(connection.view()));
    try {
      co_await tasks.join();
    } catch (const std::runtime_error &) {
      primary_failure_observed = true;
    }
    co_await resources.finish();
    cleanup_completed = true;
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(sibling_canceled);
  EXPECT_TRUE(primary_failure_observed);
  EXPECT_TRUE(cleanup_completed);
  pair.close();
}

TEST(UvppV3Coroutine, resourceScopeJoinsBorrowedWriteBeforeClosing) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::co::task_scope tasks(pair.loop);
  uv::co::resource_scope resources(pair.loop);
  std::string data{"borrowed write survives scoped cleanup"};
  bool write_completed = false;
  bool observed_stop = false;
  bool close_after_write = false;

  auto writer = [&](uv::tcp_connection_view connection) -> uv::co::task<void> {
    co_await connection.write(data);
    write_completed = true;
    observed_stop = co_await uv::co::stop_requested();
  };
  auto parent = [&]() -> uv::co::task<void> {
    auto connection = resources.own(std::move(*pair.client));
    pair.client.reset();
    tasks.spawn(writer(connection.view()));
    tasks.request_stop();
    co_await tasks.join();
    data.front() = 'B';
    co_await resources.finish();
    close_after_write = write_completed;
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(write_completed);
  EXPECT_TRUE(observed_stop);
  EXPECT_TRUE(close_after_write);
  EXPECT_EQ(data.front(), 'B');
  pair.close();
}

TEST(UvppV3Coroutine, resourceScopeRejectsConnectionFromAnotherLoop) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  uv::loop other_loop;
  uv::co::resource_scope resources(other_loop);

  EXPECT_THROW((void)resources.own(std::move(*pair.client)), std::logic_error);

  other_loop.close();
  pair.close();
}

TEST(UvppV3Coroutine, resourceScopeQuiescesActiveAcceptBeforeListenerClose) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  uv::loop loop;
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  int accept_deliveries = 0;
  bool accept_canceled = false;
  bool listener_finish_completed = false;
  bool accept_joined = false;

  auto parent = [&]() -> uv::co::task<void> {
    auto listener = resources.own(uv::tcp_listener{loop, uv::ipv4{"127.0.0.1", 0}});
    auto accepter = [&]() -> uv::co::task<void> {
      auto accept = listener.accept();
      try {
        (void)co_await accept;
      } catch (const uv::error &error) {
        ++accept_deliveries;
        accept_canceled = error.code().value() == UV_ECANCELED;
      }
    };
    tasks.spawn(accepter());
    co_await resources.finish();
    listener_finish_completed = true;
    // The listener-close path must have removed the accept cancellation slot.
    // A stale registration would invoke the old awaiter here and either deliver
    // a second result or access a completed frame.
    tasks.request_stop();
    co_await tasks.join();
    accept_joined = true;
    loop.stop();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(listener_finish_completed);
  EXPECT_EQ(accept_deliveries, 1);
  EXPECT_TRUE(accept_canceled);
  EXPECT_TRUE(accept_joined);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, resourceScopeOwnsListenerAndAcceptedConnection) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  uv::loop loop;
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  bool handler_completed = false;
  bool cleanup_completed = false;

  auto handler = [&](uv::tcp_connection_view) -> uv::co::task<void> {
    handler_completed = true;
    co_return;
  };
  uv::tcp_listener raw_listener(loop, uv::ipv4{"127.0.0.1", 0});
  const auto listener_address = raw_listener.local_address().to_v4();
  auto scoped_server = [&]() -> uv::co::task<void> {
    auto listener = resources.own(std::move(raw_listener));
    auto connection = resources.own(co_await listener.accept());
    tasks.spawn(handler(connection.view()));
    co_await tasks.join();
    co_await resources.finish();
    cleanup_completed = true;
    loop.stop();
  };
  auto client = [&]() -> uv::co::task<void> {
    auto connection = co_await uv::tcp_connection::connect(listener_address);
    (void)connection;
  };

  auto server_execution = uv::co::spawn(loop, scoped_server());
  auto client_execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(server_execution.rethrow_if_failed());
  EXPECT_NO_THROW(client_execution.rethrow_if_failed());
  EXPECT_TRUE(handler_completed);
  EXPECT_TRUE(cleanup_completed);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, resourceScopeRejectsListenerFromAnotherLoop) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  uv::loop listener_loop;
  uv::loop other_loop;
  uv::tcp_listener listener(listener_loop, uv::ipv4{"127.0.0.1", 0});
  uv::co::resource_scope resources(other_loop);

  EXPECT_THROW((void)resources.own(std::move(listener)), std::logic_error);

  other_loop.close();
  listener.request_close();
  listener_loop.run();
  EXPECT_NO_THROW(listener_loop.close());
}
