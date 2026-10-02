#include <atomic>
#include <barrier>
#include <chrono>
#include <functional>
#include <latch>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(UvppV3LoopPosting, acceptsMoveOnlyWorkFromProducerThreadsAndRunsItOnTheLoop) {
  uv::loop loop;
  uv::loop_posting posting(loop, 128, 8);
  auto endpoint = posting.endpoint();
  const auto loop_thread = std::this_thread::get_id();
  std::atomic_int executions = 0;
  std::atomic_bool wrong_thread = false;

  std::vector<std::thread> producers;
  for (int producer = 0; producer != 4; ++producer) {
    producers.emplace_back([endpoint, &executions, &wrong_thread, loop_thread] {
      for (int index = 0; index != 16; ++index) {
        auto payload = std::make_unique<int>(index);
        endpoint.post([payload = std::move(payload), &executions, &wrong_thread, loop_thread] {
          if (std::this_thread::get_id() != loop_thread) {
            wrong_thread = true;
          }
          EXPECT_GE(*payload, 0);
          ++executions;
        });
      }
    });
  }
  for (auto &producer : producers) {
    producer.join();
  }

  posting.request_close();
  loop.run();

  EXPECT_EQ(executions, 64);
  EXPECT_FALSE(wrong_thread);
  EXPECT_TRUE(posting.closed());
  loop.close();
}

TEST(UvppV3LoopPosting, concurrentAdmissionAndShutdownSettleEveryAcceptedPostExactlyOnce) {
  for (int round = 0; round != 16; ++round) {
    uv::loop loop;
    uv::loop_posting posting(loop, 1024, 7);
    auto endpoint = posting.endpoint();
    std::barrier race{5};
    std::latch closed{1};
    std::vector<std::vector<int>> accepted(4), delivered(4);
    std::vector<std::thread> producers;
    for (int p = 0; p != 4; ++p) {
      // Guarantee accepted work before racing, even when close wins every race.
      endpoint.post([&, p] { delivered[p].push_back(-1); });
      accepted[p].push_back(-1);
      producers.emplace_back([&, p, endpoint] {
        race.arrive_and_wait();
        for (int i = 0; i != 64; ++i) {
          const auto result = uv::ops::post(endpoint, [&, p, i] { delivered[p].push_back(i); });
          if (result) accepted[p].push_back(i);
          else EXPECT_EQ(result.error(), uv::make_error_code(UV_ECANCELED));
        }
        closed.wait();
        EXPECT_EQ(uv::ops::post(endpoint, [] {}).error(), uv::make_error_code(UV_ECANCELED));
      });
    }
    race.arrive_and_wait();
    posting.request_close();
    loop.run();
    closed.count_down();
    for (auto &producer : producers) producer.join();
    EXPECT_EQ(delivered, accepted);
    EXPECT_TRUE(posting.closed());
    loop.close();
  }
}

TEST(UvppV3LoopPosting, liveProducersWakeTheLoopAndPreserveTheirOwnOrder) {
  uv::loop loop;
  uv::loop_posting posting(loop, 1024, 3);
  auto endpoint = posting.endpoint();
  std::latch running{1};
  std::vector<std::vector<int>> delivered(4);
  int finished = 0;
  std::vector<std::thread> producers;
  endpoint.post([&] { running.count_down(); });
  for (int p = 0; p != 4; ++p) {
    producers.emplace_back([&, p, endpoint] {
      running.wait();
      for (int i = 0; i != 64; ++i) {
        endpoint.post([&, p, i] { delivered[p].push_back(i); });
      }
      endpoint.post([&] { if (++finished == 4) posting.request_close(); });
    });
  }
  loop.run();
  for (auto &producer : producers) producer.join();
  for (const auto &values : delivered) {
    ASSERT_EQ(values.size(), 64u);
    for (int i = 0; i != 64; ++i) EXPECT_EQ(values[i], i);
  }
  loop.close();
}

TEST(UvppV3LoopPosting, endpointsRemainUsableAfterOwnerAndLoopDestruction) {
  std::optional<uv::loop_posting_endpoint> endpoint;
  {
    uv::loop loop;
    {
      uv::loop_posting posting(loop);
      endpoint.emplace(posting.endpoint());
      posting.request_close();
      loop.run();
    }
    loop.close();
  }
  // Transfer the last strong reference: state destruction occurs on the producer.
  std::thread producer([copy = std::move(*endpoint)] {
    EXPECT_EQ(uv::ops::post(copy, [] {}).error(), uv::make_error_code(UV_ECANCELED));
    EXPECT_THROW(copy.post([] {}), uv::error);
  });
  endpoint.reset();
  producer.join();
}

TEST(UvppV3LoopPosting, rejectedAndDeliveredCapturesCanReenterWithoutALockedDestructor) {
  uv::loop loop;
  uv::loop_posting posting(loop, 1, 1);
  auto endpoint = posting.endpoint();
  int destroyed = 0;
  struct capture {
    std::function<void()> on_destroy;
    ~capture() { on_destroy(); }
  };
  auto payload = std::make_unique<capture>();
  payload->on_destroy = [&] { ++destroyed; posting.request_close(); };
  endpoint.post([payload = std::move(payload)] {});
  auto rejected = std::make_unique<capture>();
  rejected->on_destroy = [&] {
    ++destroyed;
    EXPECT_EQ(uv::ops::post(endpoint, [] {}).error(), uv::make_error_code(UV_EAGAIN));
  };
  EXPECT_EQ(uv::ops::post(endpoint, [payload = std::move(rejected)] {}).error(),
            uv::make_error_code(UV_EAGAIN));
  EXPECT_EQ(destroyed, 1);
  loop.run();
  EXPECT_EQ(destroyed, 2);
  EXPECT_TRUE(posting.closed());
  loop.close();
}

TEST(UvppV3LoopPosting, drainBudgetAllowsTimerProgressBeforeBacklogCompletes) {
  uv::loop loop;
  uv::loop_posting posting(loop, 128, 4);
  auto endpoint = posting.endpoint();
  uv::timer timer(loop);
  int delivered = 0;
  bool timer_during_backlog = false;
  endpoint.post([&] {
    ++delivered;
    timer.start(std::chrono::milliseconds{0}, [&](uv::timer &self) {
      timer_during_backlog = delivered > 0 && delivered < 128;
      self.close();
    });
  });
  for (int i = 1; i != 128; ++i) endpoint.post([&] { ++delivered; });
  posting.request_close();
  loop.run();
  EXPECT_TRUE(timer_during_backlog);
  EXPECT_EQ(delivered, 128);
  loop.close();
}

TEST(UvppV3LoopPosting, multipleCloseWaitersSurviveOwnerReleaseDuringCompletion) {
  uv::loop loop;
  auto posting = std::make_unique<uv::loop_posting>(loop);
  int completions = 0;
  auto first = [&]() -> uv::co::task<void> {
    co_await posting->close();
    posting.reset();
    ++completions;
  };
  auto second = [&]() -> uv::co::task<void> {
    co_await posting->close();
    ++completions;
  };
  auto a = uv::co::spawn(loop, first());
  auto b = uv::co::spawn(loop, second());
  loop.run();
  EXPECT_EQ(completions, 2);
  EXPECT_NO_THROW(a.rethrow_if_failed());
  EXPECT_NO_THROW(b.rethrow_if_failed());
  loop.close();
}

TEST(UvppV3LoopPosting, failureHandlerCanReplaceItselfAndReenterPosting) {
  uv::loop loop;
  uv::loop_posting posting(loop);
  auto endpoint = posting.endpoint();
  int first = 0, second = 0, followup = 0;
  posting.set_failure_handler([&, owned = std::make_shared<int>(42)](std::exception_ptr) {
    ++first;
    posting.set_failure_handler([&](std::exception_ptr) { ++second; });
    EXPECT_EQ(*owned, 42);
    endpoint.post([&] { ++followup; posting.request_close(); });
  });
  endpoint.post([] { throw 1; });
  endpoint.post([] { throw 2; });
  loop.run();
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 1);
  EXPECT_EQ(followup, 1);
  loop.close();
}

TEST(UvppV3LoopPosting, closeReleasesFailureHandlerCapturesEvenWithSurvivingEndpoints) {
  uv::loop loop;
  uv::loop_posting posting(loop);
  auto endpoint = posting.endpoint();
  auto capture = std::make_shared<int>(42);
  std::weak_ptr<int> observed = capture;
  posting.set_failure_handler([capture, endpoint](std::exception_ptr) {});
  capture.reset();
  posting.request_close();
  loop.run();
  EXPECT_TRUE(observed.expired());
  loop.close();
}

TEST(UvppV3LoopPosting, closeChecksAffinityAndIsIdempotentAfterCompletion) {
  uv::loop loop, other;
  uv::loop_posting posting(loop);
  auto wrong = [&]() -> uv::co::task<void> { co_await posting.close(); };
  auto rejected = uv::co::spawn(other, wrong());
  EXPECT_THROW(rejected.rethrow_if_failed(), std::logic_error);
  bool delivered = false;
  posting.endpoint().post([&] { delivered = true; });
  posting.request_close();
  posting.request_close();
  loop.run();
  EXPECT_TRUE(delivered);
  auto close = [&]() -> uv::co::task<void> { co_await posting.close(); };
  auto completed = uv::co::spawn(loop, close());
  EXPECT_TRUE(completed.done());
  EXPECT_NO_THROW(completed.rethrow_if_failed());
  loop.close();
  other.close();
}

TEST(UvppV3LoopPosting, invalidCapacityOrBudgetLeavesNoNativeHandle) {
  uv::loop loop;
  EXPECT_THROW(uv::loop_posting(loop, 0, 1), std::invalid_argument);
  EXPECT_THROW(uv::loop_posting(loop, 1, 0), std::invalid_argument);
  loop.close();
}

TEST(UvppV3LoopPostingDeathTest, destructionBeforeNativeCloseCompletionTerminates) {
  EXPECT_DEATH({ uv::loop loop; uv::loop_posting posting(loop); posting.request_close(); }, "");
}

TEST(UvppV3LoopPostingDeathTest, unhandledCallableFailureTerminates) {
  EXPECT_DEATH({
    uv::loop loop;
    uv::loop_posting posting(loop);
    posting.endpoint().post([] { throw 1; });
    loop.run();
  }, "");
}

TEST(UvppV3LoopPostingDeathTest, throwingFailureHandlerTerminates) {
  EXPECT_DEATH({
    uv::loop loop;
    uv::loop_posting posting(loop);
    posting.set_failure_handler([](std::exception_ptr) { throw 2; });
    posting.endpoint().post([] { throw 1; });
    loop.run();
  }, "");
}

TEST(UvppV3LoopPosting, rejectsFullAndClosedQueuesThroughTheExplicitResultSurface) {
  uv::loop loop;
  uv::loop_posting posting(loop, 1, 1);
  auto endpoint = posting.endpoint();
  int executed = 0;

  EXPECT_TRUE(uv::ops::post(endpoint, [&] { ++executed; }));
  const auto full = uv::ops::post(endpoint, [] {});
  EXPECT_EQ(full.error(), uv::make_error_code(UV_EAGAIN));

  posting.request_close();
  loop.run();
  EXPECT_EQ(executed, 1);

  const auto closed = uv::ops::post(endpoint, [] {});
  EXPECT_EQ(closed.error(), uv::make_error_code(UV_ECANCELED));
  loop.close();
}

TEST(UvppV3LoopPosting, routesCallableExceptionsAndStillDrainsAcceptedWork) {
  uv::loop loop;
  uv::loop_posting posting(loop, 8, 2);
  auto endpoint = posting.endpoint();
  int failures = 0;
  int completed = 0;
  posting.set_failure_handler([&](std::exception_ptr failure) {
    ++failures;
    EXPECT_NE(failure, nullptr);
  });

  endpoint.post([] { throw 42; });
  endpoint.post([&] { ++completed; });
  posting.request_close();
  loop.run();

  EXPECT_EQ(failures, 1);
  EXPECT_EQ(completed, 1);
  loop.close();
}

TEST(UvppV3LoopPosting, closeRequestedFromWorkStillDrainsTheCurrentBatch) {
  uv::loop loop;
  uv::loop_posting posting(loop, 8, 2);
  auto endpoint = posting.endpoint();
  int completed = 0;

  endpoint.post([&] {
    posting.request_close();
    ++completed;
  });
  endpoint.post([&] { ++completed; });
  loop.run();

  EXPECT_EQ(completed, 2);
  EXPECT_TRUE(posting.closed());
  loop.close();
}

TEST(UvppV3LoopPosting, closeAwaiterDrainsPostsBeforeResumingTheTask) {
  uv::loop loop;
  uv::loop_posting posting(loop);
  auto endpoint = posting.endpoint();
  bool work_ran = false;
  bool close_completed = false;

  endpoint.post([&] { work_ran = true; });
  auto close = [&]() -> uv::co::task<void> {
    co_await posting.close();
    EXPECT_TRUE(work_ran);
    close_completed = true;
  };
  auto execution = uv::co::spawn(loop, close());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(close_completed);
  loop.close();
}
