#include "co-test-support.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "uvpp/co/sleep.hpp"
#include "uvpp/co/task_scope.hpp"
#include "uvpp/handles/timer.hpp"
#include "uvpp/net/dns.hpp"

using namespace std::chrono_literals;
using namespace uvpp::test;

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

TEST(UvppV3Coroutine, resolveReturnsOwnedAddressesOnTheSpawnLoop) {
  uv::loop loop;
  std::optional<uv::resolved_addresses> addresses;

  auto lookup = [&]() -> uv::co::task<void> {
    addresses.emplace(co_await uv::resolve("localhost", "80", uv::resolve_options{
      .family = AF_INET,
      .socket_type = SOCK_STREAM,
    }));
  };

  auto execution = uv::co::spawn(loop, lookup());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(addresses.has_value());
  EXPECT_FALSE(addresses->empty());
  EXPECT_EQ(addresses->front().family, AF_INET);
  loop.close();
}

TEST(UvppV3Coroutine, resolveOpsReportsNativeFailureAsAResult) {
  uv::loop loop;
  std::optional<uv::resolve_result> outcome;

  auto lookup = [&]() -> uv::co::task<void> {
    outcome.emplace(co_await uv::ops::resolve("localhost", "80", uv::resolve_options{
      .socket_type = -1,
    }));
  };

  auto execution = uv::co::spawn(loop, lookup());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(outcome.has_value());
  EXPECT_FALSE(*outcome);
  EXPECT_TRUE(outcome->error());
  loop.close();
}

TEST(UvppV3Coroutine, resolveThrowsNativeFailureAtTheAwait) {
  uv::loop loop;

  auto lookup = [&]() -> uv::co::task<void> {
    (void)co_await uv::resolve("localhost", "80", uv::resolve_options{
      .socket_type = -1,
    });
  };

  auto execution = uv::co::spawn(loop, lookup());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_THROW(execution.rethrow_if_failed(), uv::error);
  loop.close();
}

TEST(UvppV3Coroutine, resolveStopRetainsTheRequestUntilItsTerminalCallback) {
  uv::loop loop;
  std::optional<uv::resolve_result> outcome;

  auto lookup = [&]() -> uv::co::task<void> {
    outcome.emplace(co_await uv::ops::resolve("localhost"));
  };

  auto execution = uv::co::spawn(loop, lookup());
  execution.request_stop();
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(outcome.has_value());
  // uv_cancel races the worker: either its terminal UV_ECANCELED callback or
  // any normal resolver completion (success or DNS failure) is valid, but the
  // root cannot complete before one of those terminal callbacks.
  loop.close();
}

TEST(UvppV3Coroutine, resolvePreexistingStopCompletesWithoutSubmitting) {
  uv::loop loop;
  uv::co::task_scope scope(loop);
  std::optional<uv::resolve_result> outcome;
  bool stop_observed = false;

  auto lookup = [&]() -> uv::co::task<void> {
    stop_observed = co_await uv::co::stop_requested();
    outcome.emplace(co_await uv::ops::resolve("localhost", "80"));
  };
  auto parent = [&]() -> uv::co::task<void> {
    // task_scope starts its children immediately.  Stopping it first means the
    // child reaches resolve with its inherited cancellation state already set.
    scope.request_stop();
    scope.spawn(lookup());
    co_await scope.join();
  };

  auto execution = uv::co::spawn(loop, parent());

  // A submitted uv_getaddrinfo request completes only through a later native
  // callback.  Synchronous root completion therefore proves that resolve took
  // its pre-submission stop path instead.
  EXPECT_TRUE(execution.done());
  EXPECT_TRUE(stop_observed);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_FALSE(*outcome);
  EXPECT_EQ(outcome->error(), uv::make_error_code(UV_ECANCELED));
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_NO_THROW(loop.close());
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

TEST(UvppV3Coroutine, spawnHandleJoinsMultipleTasksAndConsumesValueOnce) {
  uv::loop loop;
  int joiners_completed = 0;

  auto worker = []() -> uv::co::task<int> {
    co_await uv::co::sleep_for(1ms);
    co_return 42;
  };
  auto execution = uv::co::spawn(loop, worker());

  auto joiner = [&]() -> uv::co::task<void> {
    co_await execution.join();
    ++joiners_completed;
  };
  auto first_joiner = uv::co::spawn(loop, joiner());
  auto second_joiner = uv::co::spawn(loop, joiner());

  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_TRUE(execution.has_result());
  EXPECT_EQ(joiners_completed, 2);
  EXPECT_NO_THROW(first_joiner.rethrow_if_failed());
  EXPECT_NO_THROW(second_joiner.rethrow_if_failed());
  EXPECT_EQ(execution.take_result(), 42);
  EXPECT_FALSE(execution.has_result());
  EXPECT_THROW(execution.take_result(), std::logic_error);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, spawnHandleMovesMoveOnlyResult) {
  uv::loop loop;

  auto worker = []() -> uv::co::task<std::unique_ptr<int>> {
    co_await uv::co::sleep_for(0ms);
    co_return std::make_unique<int>(42);
  };
  auto initial = uv::co::spawn(loop, worker());
  auto execution = std::move(initial);

  loop.run();

  ASSERT_TRUE(execution.has_result());
  auto result = execution.take_result();
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, 42);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, spawnHandleMoveAssignmentReleasesCompletedDestination) {
  uv::loop loop;

  auto first_root = []() -> uv::co::task<int> { co_return 1; };
  auto second_root = []() -> uv::co::task<int> { co_return 2; };
  auto destination = uv::co::spawn(loop, first_root());
  auto source = uv::co::spawn(loop, second_root());
  ASSERT_TRUE(destination.done());
  ASSERT_TRUE(source.done());

  destination = std::move(source);

  EXPECT_TRUE(destination.done());
  EXPECT_EQ(destination.take_result(), 2);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, spawnHandleRetainsFailureForJoinAndObservation) {
  uv::loop loop;
  bool joined = false;

  auto worker = []() -> uv::co::task<int> {
    co_await uv::co::sleep_for(0ms);
    throw std::runtime_error{"expected root failure"};
  };
  auto execution = uv::co::spawn(loop, worker());
  auto joiner = [&]() -> uv::co::task<void> {
    co_await execution.join();
    joined = true;
  };
  auto joiner_execution = uv::co::spawn(loop, joiner());

  loop.run();

  EXPECT_TRUE(joined);
  EXPECT_NO_THROW(joiner_execution.rethrow_if_failed());
  EXPECT_FALSE(execution.has_result());
  EXPECT_THROW(execution.rethrow_if_failed(), std::runtime_error);
  EXPECT_THROW(execution.take_result(), std::runtime_error);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, spawnHandleJoinsAfterCompletion) {
  uv::loop loop;
  bool joined = false;

  auto worker = []() -> uv::co::task<void> { co_return; };
  auto execution = uv::co::spawn(loop, worker());
  ASSERT_TRUE(execution.done());

  auto joiner = [&]() -> uv::co::task<void> {
    co_await execution.join();
    joined = true;
  };
  auto joiner_execution = uv::co::spawn(loop, joiner());

  EXPECT_TRUE(joined);
  EXPECT_TRUE(joiner_execution.done());
  EXPECT_NO_THROW(joiner_execution.rethrow_if_failed());
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, spawnHandleRejectsJoinFromAnotherLoop) {
  uv::loop loop;
  uv::loop other_loop;
  bool rejected = false;

  auto worker = []() -> uv::co::task<void> {
    co_await uv::co::sleep_for(1ms);
  };
  auto execution = uv::co::spawn(loop, worker());
  auto joiner = [&]() -> uv::co::task<void> {
    try {
      co_await execution.join();
    } catch (const std::logic_error &) {
      rejected = true;
    }
  };
  auto wrong_execution = uv::co::spawn(other_loop, joiner());

  EXPECT_TRUE(wrong_execution.done());
  EXPECT_TRUE(rejected);
  loop.run();
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(loop.close());
  EXPECT_NO_THROW(other_loop.close());
}

TEST(UvppV3Coroutine, spawnHandleStopCancelsRootAndNestedChildren) {
  uv::loop loop;
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
  auto worker = [&]() -> uv::co::task<void> {
    co_await child();
  };
  auto execution = uv::co::spawn(loop, worker());
  execution.request_stop();
  EXPECT_TRUE(execution.stop_requested());

  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_TRUE(canceled);
  EXPECT_TRUE(observed_stop);
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, spawnHandleStopWaitsForSubmittedBorrowedWrite) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback TCP is not permitted in this environment";
  }

  connected_tcp_pair pair;
  pair.connect();
  std::string data{"borrowed root write survives stop"};
  bool write_completed = false;
  bool observed_stop = false;
  bool joined = false;

  auto root = [&]() -> uv::co::task<void> {
    co_await pair.client->write(data);
    write_completed = true;
    observed_stop = co_await uv::co::stop_requested();
  };
  auto execution = uv::co::spawn(pair.loop, root());
  execution.request_stop();

  // The submitted write remains active after stop. It still borrows data and
  // prevents close until its actual native completion callback runs.
  int close_status = 0;
  try {
    pair.client->request_close();
  } catch (const uv::error &error) {
    close_status = error.code().value();
  }
  EXPECT_EQ(close_status, UV_EBUSY);
  EXPECT_FALSE(execution.done());
  EXPECT_FALSE(write_completed);

  auto joiner = [&]() -> uv::co::task<void> {
    co_await execution.join();
    joined = true;
    pair.loop.stop();
  };
  auto joiner_execution = uv::co::spawn(pair.loop, joiner());
  EXPECT_FALSE(joined);

  pair.loop.run();

  EXPECT_TRUE(write_completed);
  EXPECT_TRUE(observed_stop);
  EXPECT_TRUE(execution.done());
  EXPECT_TRUE(joined);
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_NO_THROW(joiner_execution.rethrow_if_failed());
  data.front() = 'B'; // The borrow has ended only after root completion/join.
  EXPECT_EQ(data.front(), 'B');
  pair.close();
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
