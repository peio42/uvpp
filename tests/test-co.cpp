#include <chrono>
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
