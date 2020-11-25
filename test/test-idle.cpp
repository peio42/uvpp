#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

static uv::Idle *idle_handle;
static uv::Check *check_handle;
static uv::Timer *timer_handle;

static int idle_cb_called;
static int check_cb_called;
static int timer_cb_called;
static int closed_cb_called;


class IdleTest : public testing::Test {
protected:
  uv::Loop *loop = uv::Loop::getDefault();

  void SetUp() {
    idle_cb_called = 0;
    check_cb_called = 0;
    timer_cb_called = 0;
    closed_cb_called = 0;
  }
};


TEST_F(IdleTest, idle_starvation) {
  uv::Idle idle(loop);
  idle_handle = &idle;
  idle.start([](uv::Idle *idle) {
      ASSERT_EQ(idle, idle_handle);

      idle_cb_called++;
    });

  uv::Check check(loop);
  check_handle = &check;
  check.start([](uv::Check *check) {
      ASSERT_EQ(check, check_handle);

      check_cb_called++;
    });

  uv::Timer timer(loop);
  timer_handle = &timer;
  timer.start([](uv::Timer *timer) {
      ASSERT_EQ(timer, timer_handle);

      idle_handle->close([](uv::Idle *idle) { closed_cb_called++; });
      check_handle->close([](uv::Check *check) { closed_cb_called++; });
      timer_handle->close([](uv::Timer *timer) { closed_cb_called++; });

      timer_cb_called++;
    }, 50);

  loop->run();

  ASSERT_GT(idle_cb_called, 0);
  ASSERT_EQ(timer_cb_called, 1);
  ASSERT_EQ(closed_cb_called, 3);
}
