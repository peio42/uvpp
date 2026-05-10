#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Idle, runsRuntimeCallback) {
  uvpp::loop loop;
  uvpp::idle idle(loop);

  int marker = 42;
  int called = 0;

  idle.user_data(marker);
  idle.start([&](uvpp::idle &self) {
    called++;
    EXPECT_EQ(self.user_data<int>(), &marker);
    self.close();
  });

  loop.run();
  EXPECT_EQ(called, 1);
  loop.close();
}

TEST(Uvpp2Idle, startReplacesRuntimeCallbackSlot) {
  uvpp::loop loop;
  uvpp::idle idle(loop);

  int replaced = 0;
  int called = 0;

  idle.start([&](uvpp::idle &) {
    replaced++;
  });

  idle.start([&](uvpp::idle &self) {
    called++;
    self.close();
  });

  loop.run();
  EXPECT_EQ(replaced, 0);
  EXPECT_EQ(called, 1);
  loop.close();
}

static int static_idle_called = 0;

static void on_static_idle(uvpp::idle &idle) {
  static_idle_called++;
  idle.close();
}

TEST(Uvpp2Idle, runsStaticCallback) {
  uvpp::loop loop;
  uvpp::idle idle(loop);
  static_idle_called = 0;

  idle.start_static<on_static_idle>();

  loop.run();
  EXPECT_EQ(static_idle_called, 1);
  loop.close();
}

TEST(Uvpp2Prepare, runsRuntimeCallback) {
  uvpp::loop loop;
  uvpp::prepare prepare(loop);

  int called = 0;

  prepare.start([&](uvpp::prepare &self) {
    called++;
    self.close();
  });

  loop.run();
  EXPECT_EQ(called, 1);
  loop.close();
}

static int static_prepare_called = 0;

static void on_static_prepare(uvpp::prepare &prepare) {
  static_prepare_called++;
  prepare.close();
}

TEST(Uvpp2Prepare, runsStaticCallback) {
  uvpp::loop loop;
  uvpp::prepare prepare(loop);
  static_prepare_called = 0;

  prepare.start_static<on_static_prepare>();

  loop.run();
  EXPECT_EQ(static_prepare_called, 1);
  loop.close();
}

TEST(Uvpp2Check, runsRuntimeCallback) {
  uvpp::loop loop;
  uvpp::check check(loop);
  uvpp::idle driver(loop);

  int called = 0;

  driver.start([](uvpp::idle &) {});
  check.start([&](uvpp::check &self) {
    called++;
    driver.close();
    self.close();
  });

  loop.run();
  EXPECT_EQ(called, 1);
  loop.close();
}

static int static_check_called = 0;
static uvpp::idle *static_check_driver = nullptr;

static void on_static_check(uvpp::check &check) {
  static_check_called++;
  static_check_driver->close();
  check.close();
}

TEST(Uvpp2Check, runsStaticCallback) {
  uvpp::loop loop;
  uvpp::check check(loop);
  uvpp::idle driver(loop);
  static_check_called = 0;
  static_check_driver = &driver;

  driver.start([](uvpp::idle &) {});
  check.start_static<on_static_check>();

  loop.run();
  EXPECT_EQ(static_check_called, 1);
  static_check_driver = nullptr;
  loop.close();
}

TEST(Uvpp2Async, runsRuntimeCallback) {
  uvpp::loop loop;
  uvpp::async async(loop);

  int marker = 7;
  int called = 0;

  async.user_data(marker);
  async.set_callback([&](uvpp::async &self) {
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

static void on_static_async(uvpp::async &async) {
  static_async_called++;
  async.close();
}

TEST(Uvpp2Async, runsStaticCallback) {
  uvpp::loop loop;
  uvpp::async async(loop, uvpp::async::static_callback<on_static_async>{});
  static_async_called = 0;

  async.send();

  loop.run();
  EXPECT_EQ(static_async_called, 1);
  loop.close();
}
