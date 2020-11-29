#include <vector>
#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

static int once_cb_called;
static int once_close_cb_called;
static int repeat_cb_called;
static int repeat_close_cb_called;
static int order_cb_called;

static uv::Timer *tiny_timer;
static uv::Timer *huge_timer1;
static uv::Timer *huge_timer2;

static unsigned int timer_run_once_timer_cb_called;

static uint64_t timer_early_check_expected_time;

static int repeat_1_cb_called;
static int repeat_2_cb_called;
static int close_cb_called;
static bool repeat_2_cb_allowed;


class TimerTest : public testing::Test {
protected:
  uv::Loop *loop = uv::Loop::getDefault();

  void SetUp() {
    once_cb_called = 0;
    once_close_cb_called = 0;
    repeat_cb_called = 0;
    repeat_close_cb_called = 0;
    order_cb_called = 0;

    repeat_1_cb_called = 0;
    repeat_2_cb_called = 0;
    close_cb_called = 0;
    repeat_2_cb_allowed = false;
  }

  static void once_close_cb(uv::Timer *timer) {
    // printf("once_close-cb\n");
    ASSERT_NE(timer, nullptr);
    ASSERT_FALSE(timer->active());

    once_close_cb_called++;
  }

  static void once_cb(uv::Timer *timer) {
    // printf("once-cb %d\n", once_cb_called);
    ASSERT_NE(timer, nullptr);
    ASSERT_FALSE(timer->active());

    once_cb_called++;

    timer->close(once_close_cb);
  }

  static void repeat_close_cb(uv::Timer *timer) {
    // printf("repeat-close-cb\n");
    ASSERT_NE(timer, nullptr);

    repeat_close_cb_called++;
  }

  static void repeat_cb(uv::Timer *timer) {
    // printf("repeat-cb\n");
    ASSERT_NE(timer, nullptr);
    ASSERT_TRUE(timer->active());

    repeat_cb_called++;

    if (repeat_cb_called == 5) {
      timer->close(repeat_close_cb);
    }
  }

  static void never_cb(uv::Timer *timer) {
    // printf("never-cb should never be called\n");
    FAIL();
  }


  static void order_cb(uv::Timer *timer) {
    ASSERT_EQ(order_cb_called++, * timer->get<int>());
  }

  static void tiny_timer_cb(uv::Timer *timer) {
    ASSERT_EQ(timer, tiny_timer);
    tiny_timer->close();
    huge_timer1->close();
    huge_timer2->close();
  }

  static void huge_repeat_cb(uv::Timer *timer) {
    static int ncalls;

    if (ncalls == 0)
      ASSERT_EQ(timer, huge_timer1);
    else
      ASSERT_EQ(timer, tiny_timer);

    if (++ncalls == 10) {
      tiny_timer->close();
      huge_timer1->close();
    }
  }

  static void timer_run_once_timer_cb(uv::Timer *timer) {
    timer_run_once_timer_cb_called++;
  }
};


TEST_F(TimerTest, timer) {
  uint64_t start_time = loop->now();

  std::vector<uv::Timer> once_timers(10, loop);

  auto i = -50;
  for (auto &once : once_timers) {
    once.start(once_cb, i += 50);
  }

  uv::Timer repeat(loop);
  repeat.start(repeat_cb, 100, 100);

  uv::Timer never(loop);
  never.start(never_cb, 100, 100);
  never.stop();
  never.unref();

  loop->run();

  ASSERT_EQ(once_cb_called, 10);
  ASSERT_EQ(once_close_cb_called, 10);
  // printf("repeat_cb_called %d\n", repeat_cb_called);
  ASSERT_EQ(repeat_cb_called, 5);
  ASSERT_EQ(repeat_close_cb_called, 1);
  ASSERT_GE(loop->now() - start_time, 500UL);
}


TEST_F(TimerTest, timer_start_twice) {
  uv::Timer once(loop);

  once.start(never_cb, 86400 * 1000);
  once.start(once_cb, 10);

  loop->run();

  ASSERT_EQ(once_cb_called, 1);
}


TEST_F(TimerTest, timer_init) {
  uv::Timer timer(loop);

  ASSERT_EQ(timer.get_repeat(), 0UL);
  ASSERT_FALSE(timer.active());
}


TEST_F(TimerTest, timer_order) {
  uv::Timer timer_a(loop);
  int first = 0;
  timer_a.set(&first);
  timer_a.start(order_cb, 0);

  uv::Timer timer_b(loop);
  int second = 1;
  timer_b.set(&second);
  timer_b.start(order_cb, 0);

  loop->run();

  ASSERT_EQ(order_cb_called, 2);
}


TEST_F(TimerTest, timer_huge_timeout) {
  uv::Timer t1(loop), t2(loop), t3(loop);

  tiny_timer = &t1;
  huge_timer1 = &t2;
  huge_timer2 = &t3;

  tiny_timer->start(tiny_timer_cb, 1);
  huge_timer1->start(tiny_timer_cb, 0xffffffffffffLL);
  huge_timer2->start(tiny_timer_cb, (uint64_t) -1);

  loop->run();
}


TEST_F(TimerTest, timer_huge_repeat) {
  uv::Timer t1(loop), t2(loop);

  tiny_timer = &t1;
  huge_timer1 = &t2;

  tiny_timer->start(huge_repeat_cb, 2, 2);
  huge_timer1->start(huge_repeat_cb, 1, (uint64_t) -1);

  loop->run();
}


TEST_F(TimerTest, timer_run_once) {
  uv::Timer timer(loop);

  timer.start(timer_run_once_timer_cb, 0, 0);
  loop->run();
  ASSERT_EQ(timer_run_once_timer_cb_called, 1UL);

  timer.start(timer_run_once_timer_cb, 1);
  loop->run();
  ASSERT_EQ(timer_run_once_timer_cb_called, 2UL);

  timer.close();
  loop->run();
}


TEST_F(TimerTest, timer_is_closing) {
  uv::Timer timer(loop);
  timer.close();

  try {
    timer.start(never_cb, 100, 100);
    FAIL();
  }
  catch (uv::Error &e) {
    ASSERT_EQ(e.err, UV_EINVAL);
  }
  catch (...) {
    FAIL();
  }

  // [Cleanup] uv__finish_close: Assertion `handle->flags & UV_HANDLE_CLOSING' failed.
  loop->run();
}


TEST_F(TimerTest, timer_null_callback) {
  uv::Timer timer(loop);

  try {
    timer.start(nullptr, 100, 100);
    FAIL();
  }
  catch (uv::Error &e) {
    ASSERT_EQ(e.err, UV_EINVAL);
  }
  catch (...) {
    FAIL();
  }
}


TEST_F(TimerTest, timer_early_check) {
  const uint64_t timeout_ms = 10;

  timer_early_check_expected_time = loop->now() + timeout_ms;

  try {
    uv::Timer timer(loop);
    timer.start([](uv::Timer *timer) {
        uint64_t hrtime = uv::hrtime() / 1000000;
        ASSERT_GE(hrtime, timer_early_check_expected_time);
      }, timeout_ms);
    loop->run();

    timer.close();
    loop->run();
  } catch (uv::Error &e) {
    FAIL();
  }
}


uv::Timer *repeat_1_ptr;
uv::Timer *repeat_2_ptr;

TEST_F(TimerTest, timer_again) {
  uint64_t start_time = loop->now();
  ASSERT_GT(start_time, 0UL);

  uv::Timer dummy(loop);
  try {
    dummy.again();
    FAIL();
  }
  catch (uv::Error &e) {
    ASSERT_EQ(e.err, UV_EINVAL);
  }
  catch (...) {
    FAIL();
  }
  dummy.unref();


  uv::Timer repeat_1(loop);
  repeat_1_ptr = &repeat_1;
  repeat_1.start([](uv::Timer *timer) {
      ASSERT_EQ(timer, repeat_1_ptr);
      ASSERT_EQ(timer->get_repeat(), 50UL);

      repeat_1_cb_called++;

      repeat_2_ptr->again();

      if (repeat_1_cb_called == 10) {
        timer->close([](uv::Timer *timer) {
            ASSERT_EQ(timer, repeat_1_ptr);
            close_cb_called++;
          });
        repeat_2_cb_allowed = true;
      }
    }, 50);
  ASSERT_EQ(repeat_1.get_repeat(), 0UL);

  repeat_1.set_repeat(50);
  ASSERT_EQ(repeat_1.get_repeat(), 50UL);


  uv::Timer repeat_2(loop);
  repeat_2_ptr = &repeat_2;
  repeat_2.start([](uv::Timer *timer) {
      ASSERT_EQ(timer, repeat_2_ptr);
      ASSERT_TRUE(repeat_2_cb_allowed);

      repeat_2_cb_called++;

      if (timer->get_repeat() == 0) {
        ASSERT_FALSE(timer->active());
        timer->close([](uv::Timer *timer) {
            ASSERT_EQ(timer, repeat_2_ptr);
            close_cb_called++;
          });
        return ;
      }

      ASSERT_EQ(timer->get_repeat(), 100UL);
      timer->set_repeat(0);
    }, 100, 100);
  ASSERT_EQ(repeat_2.get_repeat(), 100UL);

  loop->run();

  ASSERT_EQ(repeat_1_cb_called, 10);
  ASSERT_EQ(repeat_2_cb_called, 2);
  ASSERT_EQ(close_cb_called, 2);
}
