#include <chrono>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

using namespace std::chrono_literals;

TEST(Uvpp2Timer, runsRuntimeCallback) {
  uvpp::loop loop;
  uvpp::timer timer(loop);

  int called = 0;
  int marker = 42;
  bool closed = false;

  timer.user_data(marker);
  timer.start(1ms, [&](uvpp::timer &self) {
    called++;
    EXPECT_EQ(self.user_data<int>(), &marker);
    self.close([&](uvpp::timer &) {
      closed = true;
    });
  });

  loop.run();
  EXPECT_EQ(called, 1);
  EXPECT_TRUE(closed);
  loop.close();
}

TEST(Uvpp2Timer, startReplacesRuntimeCallbackSlot) {
  uvpp::loop loop;
  uvpp::timer timer(loop);

  int replaced = 0;
  int called = 0;

  timer.start(50ms, [&](uvpp::timer &) {
    replaced++;
  });

  timer.start(1ms, [&](uvpp::timer &self) {
    called++;
    self.close();
  });

  loop.run();
  EXPECT_EQ(replaced, 0);
  EXPECT_EQ(called, 1);
  loop.close();
}

static int static_timer_called = 0;
static int static_timer_marker = 0;

static void on_static_timer(uvpp::timer &timer) {
  static_timer_called++;
  EXPECT_EQ(timer.user_data<int>(), &static_timer_marker);
  timer.close();
}

TEST(Uvpp2Timer, runsStaticCallback) {
  uvpp::loop loop;
  uvpp::timer timer(loop);
  static_timer_called = 0;
  static_timer_marker = 7;
  timer.user_data(static_timer_marker);

  timer.start_static<on_static_timer>(1ms);

  loop.run();
  EXPECT_EQ(static_timer_called, 1);
  loop.close();
}
