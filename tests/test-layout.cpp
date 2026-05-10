#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Layout, reconstructsHandlesFromNativePointers) {
  static_assert(std::is_standard_layout_v<uvpp::async::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::check::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::idle::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::pipe::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::prepare::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::timer::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::tcp::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::tty::native_storage>);

  uvpp::loop loop;
  uvpp::async async(loop);
  uvpp::check check(loop);
  uvpp::idle idle(loop);
  uvpp::pipe pipe(loop);
  uvpp::prepare prepare(loop);
  uvpp::timer timer(loop);
  uvpp::tcp tcp(loop);

  EXPECT_EQ(&async, &uvpp::async::from_native(async.native()));
  EXPECT_EQ(&async, &uvpp::async::from_native(async.native_handle()));
  EXPECT_EQ(&check, &uvpp::check::from_native(check.native()));
  EXPECT_EQ(&check, &uvpp::check::from_native(check.native_handle()));
  EXPECT_EQ(&idle, &uvpp::idle::from_native(idle.native()));
  EXPECT_EQ(&idle, &uvpp::idle::from_native(idle.native_handle()));
  EXPECT_EQ(&pipe, &uvpp::pipe::from_native(pipe.native()));
  EXPECT_EQ(&pipe, &uvpp::pipe::from_native(pipe.native_handle()));
  EXPECT_EQ(&prepare, &uvpp::prepare::from_native(prepare.native()));
  EXPECT_EQ(&prepare, &uvpp::prepare::from_native(prepare.native_handle()));
  EXPECT_EQ(&timer, &uvpp::timer::from_native(timer.native()));
  EXPECT_EQ(&timer, &uvpp::timer::from_native(timer.native_handle()));
  EXPECT_EQ(&tcp, &uvpp::tcp::from_native(tcp.native()));
  EXPECT_EQ(&tcp, &uvpp::tcp::from_native(tcp.native_handle()));

  async.close();
  check.close();
  idle.close();
  pipe.close();
  prepare.close();
  timer.close();
  tcp.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Layout, reconstructsTtyFromNativePointersWhenAvailable) {
  if (uv_guess_handle(1) != UV_TTY) {
    GTEST_SKIP() << "stdout is not a tty";
  }

  uvpp::loop loop;
  uvpp::tty tty(loop, 1, false);

  EXPECT_EQ(&tty, &uvpp::tty::from_native(tty.native()));
  EXPECT_EQ(&tty, &uvpp::tty::from_native(tty.native_handle()));

  tty.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Layout, reconstructsRequestsFromNativePointers) {
  static_assert(std::is_standard_layout_v<uvpp::write_request::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::connect_request::native_storage>);

  uvpp::write_request write;
  uvpp::connect_request connect;

  EXPECT_EQ(&write, &uvpp::write_request::from_native(write.native()));
  EXPECT_EQ(&write, &uvpp::write_request::from_native(write.native_request()));
  EXPECT_EQ(&connect, &uvpp::connect_request::from_native(connect.native()));
  EXPECT_EQ(&connect, &uvpp::connect_request::from_native(connect.native_request()));
}
