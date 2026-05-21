#include <chrono>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

using namespace std::chrono_literals;

TEST(Uvpp2Timer, runsRuntimeCallback) {
  uv::loop loop;
  uv::timer timer(loop);

  int called = 0;
  int marker = 42;
  bool closed = false;

  timer.user_data(marker);
  timer.start(1ms, [&](uv::timer &self) {
    called++;
    EXPECT_EQ(self.user_data<int>(), &marker);
    self.close([&](uv::timer &) {
      closed = true;
    });
  });

  loop.run();
  EXPECT_EQ(called, 1);
  EXPECT_TRUE(closed);
  loop.close();
}

TEST(Uvpp2Timer, startReplacesRuntimeCallbackSlot) {
  uv::loop loop;
  uv::timer timer(loop);

  int replaced = 0;
  int called = 0;

  timer.start(50ms, [&](uv::timer &) {
    replaced++;
  });

  timer.start(1ms, [&](uv::timer &self) {
    called++;
    self.close();
  });

  loop.run();
  EXPECT_EQ(replaced, 0);
  EXPECT_EQ(called, 1);
  loop.close();
}

TEST(Uvpp2Timer, exposesRepeatInterval) {
  uv::loop loop;
  uv::timer timer(loop);

  EXPECT_EQ(timer.repeat(), 0ms);

  timer.set_repeat(25ms);
  EXPECT_EQ(timer.repeat(), 25ms);

  timer.start(1ms, 10ms, [&](uv::timer &self) {
    EXPECT_EQ(self.repeat(), 10ms);
    self.set_repeat(0ms);
    EXPECT_EQ(self.repeat(), 0ms);
    self.close();
  });

  loop.run();
  loop.close();
}

#if UVPP_HAS_TIMER_GET_DUE_IN
TEST(Uvpp2Timer, exposesDueIn) {
  uv::loop loop;
  uv::timer timer(loop);

  timer.start(1s, [](uv::timer &) {});

  auto due = timer.due_in();
  EXPECT_GT(due, 0ms);
  EXPECT_LE(due, 1s);

  timer.stop();
  timer.close();
  loop.run();
  loop.close();
}
#endif

static int static_timer_called = 0;
static int static_timer_marker = 0;

static void on_static_timer(uv::timer &timer) {
  static_timer_called++;
  EXPECT_EQ(timer.user_data<int>(), &static_timer_marker);
  timer.close();
}

TEST(Uvpp2Timer, runsStaticCallback) {
  uv::loop loop;
  uv::timer timer(loop);
  static_timer_called = 0;
  static_timer_marker = 7;
  timer.user_data(static_timer_marker);

  timer.start_static<on_static_timer>(1ms);

  loop.run();
  EXPECT_EQ(static_timer_called, 1);
  loop.close();
}
