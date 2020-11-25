#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

static uv::Prepare *prepare_handle;
static uv::Check *check_handle;
static uv::Timer *timer_handle;

static int prepare_cb_called;
static int check_cb_called;
static int timer_cb_called;


class TimerFromCheckTest : public testing::Test {
protected:
  uv::Loop *loop = uv::Loop::getDefault();

  void SetUp() {
    prepare_cb_called = 0;
    check_cb_called = 0;
    timer_cb_called = 0;
  }

  static void timer_cb(uv::Timer *timer) {
    timer->stop();

    ASSERT_EQ(prepare_cb_called, 1);
    ASSERT_EQ(check_cb_called, 1);
    ASSERT_EQ(timer_cb_called, 0);

    timer_cb_called++;
  }
};


TEST_F(TimerFromCheckTest, timer_from_check) {
  uv::Prepare prepare(loop);

  uv::Check check(loop);
  check.start([](uv::Check *check) {
      check_handle->stop();

      timer_handle->stop(); /* Runs before timer_cb. */
      timer_handle->start(timer_cb, 50);

      prepare_handle->start([](uv::Prepare *prepare) {
          prepare->stop();

          ASSERT_EQ(prepare_cb_called, 0);
          ASSERT_EQ(check_cb_called, 1);
          ASSERT_EQ(timer_cb_called, 0);

          prepare_cb_called++;
        });

      ASSERT_EQ(prepare_cb_called, 0);
      ASSERT_EQ(check_cb_called, 0);
      ASSERT_EQ(timer_cb_called, 0);

      check_cb_called++;
    });

  uv::Timer timer(loop);
  timer.start(timer_cb, 50);

  prepare_handle = &prepare;
  check_handle = &check;
  timer_handle = &timer;
  loop->run();

  ASSERT_EQ(prepare_cb_called, 1);
  ASSERT_EQ(check_cb_called, 1);
  ASSERT_EQ(timer_cb_called, 1);

  prepare.close();
  check.close();
  timer.close();

  loop->run();
}
