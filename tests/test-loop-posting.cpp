#include <atomic>
#include <memory>
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
          executions.fetch_add(*payload + 1 == 0 ? 0 : 1);
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
