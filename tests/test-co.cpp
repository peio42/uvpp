#include <chrono>
#include <memory>
#include <stdexcept>

#include "gtest/gtest.h"
#include "uvpp/co/sleep.hpp"
#include "uvpp/handles/timer.hpp"

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
