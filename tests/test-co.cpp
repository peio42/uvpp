#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"
#include "uvpp/co/resource_scope.hpp"
#include "uvpp/co/sleep.hpp"
#include "uvpp/co/task_scope.hpp"
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

class synchronous_stop_awaiter {
public:
  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, uv::co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
    continuation_ = continuation;
    auto *cancellation = continuation.promise().cancellation();
    return cancellation != nullptr && cancellation->register_callback(
        registration_, &synchronous_stop_awaiter::on_stop, this);
  }

  void await_resume() const noexcept {}

private:
  static void on_stop(void *context) noexcept {
    static_cast<synchronous_stop_awaiter *>(context)->continuation_.resume();
  }

  uv::co::detail::cancellation_registration registration_{};
  std::coroutine_handle<> continuation_{};
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

TEST(UvppV3Coroutine, taskScopeJoinsAllChildrenBeforeResuming) {
  uv::loop loop;
  uv::co::task_scope scope(loop);
  int completed = 0;
  bool joined = false;

  auto child = [&]() -> uv::co::task<void> {
    co_await uv::co::sleep_for(1ms);
    ++completed;
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(child());
    scope.spawn(child());
    co_await scope.join();
    joined = true;
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(completed, 2);
  EXPECT_TRUE(joined);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, taskScopeFailureStopsSiblingsBeforeReportingFirstFailure) {
  uv::loop loop;
  uv::co::task_scope scope(loop);
  bool sibling_canceled = false;

  auto failing_child = []() -> uv::co::task<void> {
    co_await uv::co::sleep_for(0ms);
    throw std::runtime_error{"expected scoped failure"};
  };
  auto sibling = [&]() -> uv::co::task<void> {
    try {
      co_await uv::co::sleep_for(1h);
    } catch (const uv::error &error) {
      sibling_canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(failing_child());
    scope.spawn(sibling());
    co_await scope.join();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_TRUE(sibling_canceled);
  EXPECT_THROW(execution.rethrow_if_failed(), std::runtime_error);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, taskScopeDefersJoinResumePastSynchronousSiblingStop) {
  uv::loop loop;
  uv::co::task_scope scope(loop);
  bool sibling_completed = false;

  auto failing_child = []() -> uv::co::task<void> {
    co_await uv::co::sleep_for(0ms);
    throw std::runtime_error{"expected scoped failure"};
  };
  auto sibling = [&]() -> uv::co::task<void> {
    co_await synchronous_stop_awaiter{};
    sibling_completed = true;
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(failing_child());
    scope.spawn(sibling());
    co_await scope.join();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_TRUE(sibling_completed);
  EXPECT_THROW(execution.rethrow_if_failed(), std::runtime_error);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, taskScopeRejectsJoinFromAnotherLoop) {
  uv::loop scope_loop;
  uv::loop other_loop;
  uv::co::task_scope scope(scope_loop);
  bool rejected = false;

  auto parent = [&]() -> uv::co::task<void> {
    try {
      co_await scope.join();
    } catch (const std::logic_error &) {
      rejected = true;
    }
  };

  auto execution = uv::co::spawn(other_loop, parent());
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(rejected);
  other_loop.close();
  scope_loop.close();
}

TEST(UvppV3Coroutine, taskScopeStopCancelsSleepAndJoinsChild) {
  uv::loop loop;
  uv::co::task_scope scope(loop);
  bool canceled = false;
  bool observed_stop = false;

  auto child = [&]() -> uv::co::task<void> {
    try {
      co_await uv::co::sleep_for(1h);
    } catch (const uv::error &error) {
      canceled = error.code().value() == UV_ECANCELED;
    }
    observed_stop = co_await uv::co::stop_requested();
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(child());
    scope.request_stop();
    co_await scope.join();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(canceled);
  EXPECT_TRUE(observed_stop);
  EXPECT_NO_THROW(loop.close());
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
  listener.listen([&](uv::tcp &server, uv::result status) {
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
        reinterpret_cast<uv_stream_t *>(listener.native_handle()), 0);
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
  const auto listener_address = raw_listener.sockname().to_v4();
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
  listener.close();
  listener_loop.run();
  EXPECT_NO_THROW(listener_loop.close());
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
    listener->close();
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
  const auto address = listener->sockname().to_v4();
  uv::co::task_scope scope(loop);
  int handlers_started = 0;
  int handlers_completed = 0;

  auto handle = [&](uv::tcp_connection connection) -> uv::co::task<void> {
    EXPECT_NE(connection.native_handle(), nullptr);
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
    listener->close();
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
