#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

static uv::Thread *g_thread;
static uv::Mutex *g_mutex;

static uv::Prepare *g_prepare;
static uv::Async *g_async;

static /* volatile */ int async_cb_called;
static int prepare_cb_called;
static int close_cb_called;


class AsyncTest : public testing::Test {
protected:
  uv::Loop *loop = uv::Loop::getDefault();

  void SetUp() {
    async_cb_called = 0;
    prepare_cb_called = 0;
    close_cb_called = 0;
  }
};


TEST_F(AsyncTest, async) {
  uv::Mutex mutex;
  g_mutex = &mutex;

  mutex.lock();

  uv::Prepare prepare(loop);
  g_prepare = &prepare;
  prepare.start([](uv::Prepare *prepare) {
      ASSERT_EQ(prepare, g_prepare);

      if (prepare_cb_called++)
        return ;

      g_thread = new uv::Thread([](void *arg) {
          while(true) {
            g_mutex->lock();
            auto n = async_cb_called;
            g_mutex->unlock();

            if (n == 3)
              return ;

            g_async->send();
          }
        });

      g_mutex->unlock();
    });

  uv::Async async(loop, [](uv::Async *async) {
      ASSERT_EQ(async, g_async);

      g_mutex->lock();
      auto n = ++async_cb_called;
      g_mutex->unlock();
  
      if (n == 3) {
        g_async->close([](uv::Async *async) { close_cb_called++; });
        g_prepare->close([](uv::Prepare *prepare) { close_cb_called++; });
      }
    });
  g_async = &async;

  loop->run();
  g_thread->join();

  ASSERT_GT(prepare_cb_called, 0);
  ASSERT_EQ(async_cb_called, 3);
  ASSERT_EQ(close_cb_called, 2);
}
