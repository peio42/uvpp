#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Idle, runsRuntimeCallback) {
  uv::loop loop;
  uv::idle idle(loop);

  int marker = 42;
  int called = 0;

  idle.user_data(marker);
  idle.start([&](uv::idle &self) {
    called++;
    EXPECT_EQ(self.user_data<int>(), &marker);
    self.close();
  });

  loop.run();
  EXPECT_EQ(called, 1);
  loop.close();
}

TEST(Uvpp2Idle, startReplacesRuntimeCallbackSlot) {
  uv::loop loop;
  uv::idle idle(loop);

  int replaced = 0;
  int called = 0;

  idle.start([&](uv::idle &) {
    replaced++;
  });

  idle.start([&](uv::idle &self) {
    called++;
    self.close();
  });

  loop.run();
  EXPECT_EQ(replaced, 0);
  EXPECT_EQ(called, 1);
  loop.close();
}

static int static_idle_called = 0;

static void on_static_idle(uv::idle &idle) {
  static_idle_called++;
  idle.close();
}

TEST(Uvpp2Idle, runsStaticCallback) {
  uv::loop loop;
  uv::idle idle(loop);
  static_idle_called = 0;

  idle.start_static<on_static_idle>();

  loop.run();
  EXPECT_EQ(static_idle_called, 1);
  loop.close();
}

TEST(Uvpp2Prepare, runsRuntimeCallback) {
  uv::loop loop;
  uv::prepare prepare(loop);

  int called = 0;

  prepare.start([&](uv::prepare &self) {
    called++;
    self.close();
  });

  loop.run();
  EXPECT_EQ(called, 1);
  loop.close();
}

static int static_prepare_called = 0;

static void on_static_prepare(uv::prepare &prepare) {
  static_prepare_called++;
  prepare.close();
}

TEST(Uvpp2Prepare, runsStaticCallback) {
  uv::loop loop;
  uv::prepare prepare(loop);
  static_prepare_called = 0;

  prepare.start_static<on_static_prepare>();

  loop.run();
  EXPECT_EQ(static_prepare_called, 1);
  loop.close();
}

TEST(Uvpp2Check, runsRuntimeCallback) {
  uv::loop loop;
  uv::check check(loop);
  uv::idle driver(loop);

  int called = 0;

  driver.start([](uv::idle &) {});
  check.start([&](uv::check &self) {
    called++;
    driver.close();
    self.close();
  });

  loop.run();
  EXPECT_EQ(called, 1);
  loop.close();
}

static int static_check_called = 0;
static uv::idle *static_check_driver = nullptr;

static void on_static_check(uv::check &check) {
  static_check_called++;
  static_check_driver->close();
  check.close();
}

TEST(Uvpp2Check, runsStaticCallback) {
  uv::loop loop;
  uv::check check(loop);
  uv::idle driver(loop);
  static_check_called = 0;
  static_check_driver = &driver;

  driver.start([](uv::idle &) {});
  check.start_static<on_static_check>();

  loop.run();
  EXPECT_EQ(static_check_called, 1);
  static_check_driver = nullptr;
  loop.close();
}

TEST(Uvpp2Async, runsRuntimeCallback) {
  uv::loop loop;
  uv::async async(loop);

  int marker = 7;
  int called = 0;

  async.user_data(marker);
  async.set_callback([&](uv::async &self) {
    called++;
    EXPECT_EQ(self.user_data<int>(), &marker);
    self.close();
  });

  async.send();
  loop.run();
  EXPECT_EQ(called, 1);
  loop.close();
}

static int static_async_called = 0;

static void on_static_async(uv::async &async) {
  static_async_called++;
  async.close();
}

TEST(Uvpp2Async, runsStaticCallback) {
  uv::loop loop;
  uv::async async(loop, uv::async::static_callback<on_static_async>{});
  static_async_called = 0;

  async.send();

  loop.run();
  EXPECT_EQ(static_async_called, 1);
  loop.close();
}
