#include <type_traits>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Layout, reconstructsHandlesFromNativePointers) {
  static_assert(std::is_standard_layout_v<uvpp::async::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::check::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::fs_event::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::fs_poll::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::idle::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::pipe::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::poll::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::prepare::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::process::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::signal::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::timer::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::tcp::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::tty::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::udp::native_storage>);

  uvpp::loop loop;
  uvpp::async async(loop);
  uvpp::check check(loop);
  uvpp::fs_event fs_event(loop);
  uvpp::fs_poll fs_poll(loop);
  uvpp::idle idle(loop);
  uvpp::pipe pipe(loop);
  int fds[2]{};
  ASSERT_EQ(::pipe(fds), 0);
  uvpp::poll poll(loop, fds[0]);
  uvpp::prepare prepare(loop);
  uvpp::signal signal(loop);
  uvpp::timer timer(loop);
  uvpp::tcp tcp(loop);
  uvpp::udp udp(loop);

  EXPECT_EQ(&async, &uvpp::async::from_native(async.native()));
  EXPECT_EQ(&async, &uvpp::async::from_native(async.native_handle()));
  EXPECT_EQ(&check, &uvpp::check::from_native(check.native()));
  EXPECT_EQ(&check, &uvpp::check::from_native(check.native_handle()));
  EXPECT_EQ(&fs_event, &uvpp::fs_event::from_native(fs_event.native()));
  EXPECT_EQ(&fs_event, &uvpp::fs_event::from_native(fs_event.native_handle()));
  EXPECT_EQ(&fs_poll, &uvpp::fs_poll::from_native(fs_poll.native()));
  EXPECT_EQ(&fs_poll, &uvpp::fs_poll::from_native(fs_poll.native_handle()));
  EXPECT_EQ(&idle, &uvpp::idle::from_native(idle.native()));
  EXPECT_EQ(&idle, &uvpp::idle::from_native(idle.native_handle()));
  EXPECT_EQ(&pipe, &uvpp::pipe::from_native(pipe.native()));
  EXPECT_EQ(&pipe, &uvpp::pipe::from_native(pipe.native_handle()));
  EXPECT_EQ(&poll, &uvpp::poll::from_native(poll.native()));
  EXPECT_EQ(&poll, &uvpp::poll::from_native(poll.native_handle()));
  EXPECT_EQ(&prepare, &uvpp::prepare::from_native(prepare.native()));
  EXPECT_EQ(&prepare, &uvpp::prepare::from_native(prepare.native_handle()));
  EXPECT_EQ(&signal, &uvpp::signal::from_native(signal.native()));
  EXPECT_EQ(&signal, &uvpp::signal::from_native(signal.native_handle()));
  EXPECT_EQ(&timer, &uvpp::timer::from_native(timer.native()));
  EXPECT_EQ(&timer, &uvpp::timer::from_native(timer.native_handle()));
  EXPECT_EQ(&tcp, &uvpp::tcp::from_native(tcp.native()));
  EXPECT_EQ(&tcp, &uvpp::tcp::from_native(tcp.native_handle()));
  EXPECT_EQ(&udp, &uvpp::udp::from_native(udp.native()));
  EXPECT_EQ(&udp, &uvpp::udp::from_native(udp.native_handle()));

  async.close();
  check.close();
  fs_event.close();
  fs_poll.close();
  idle.close();
  pipe.close();
  poll.close();
  prepare.close();
  signal.close();
  timer.close();
  tcp.close();
  udp.close();
  loop.run();
  loop.close();
  close(fds[0]);
  close(fds[1]);
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
  static_assert(std::is_standard_layout_v<uvpp::fs::raw::request::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::shutdown_request::native_storage>);
  static_assert(std::is_standard_layout_v<uvpp::udp_send_request::native_storage>);

  uvpp::write_request write;
  uvpp::connect_request connect;
  uvpp::fs::raw::request fs;
  uvpp::shutdown_request shutdown;
  uvpp::udp_send_request udp_send;

  EXPECT_EQ(&write, &uvpp::write_request::from_native(write.native()));
  EXPECT_EQ(&write, &uvpp::write_request::from_native(write.native_request()));
  EXPECT_EQ(&connect, &uvpp::connect_request::from_native(connect.native()));
  EXPECT_EQ(&connect, &uvpp::connect_request::from_native(connect.native_request()));
  EXPECT_EQ(&fs, &uvpp::fs::raw::request::from_native(fs.native()));
  EXPECT_EQ(&fs, &uvpp::fs::raw::request::from_native(fs.native_request()));
  EXPECT_EQ(&shutdown, &uvpp::shutdown_request::from_native(shutdown.native()));
  EXPECT_EQ(&shutdown, &uvpp::shutdown_request::from_native(shutdown.native_request()));
  EXPECT_EQ(&udp_send, &uvpp::udp_send_request::from_native(udp_send.native()));
  EXPECT_EQ(&udp_send, &uvpp::udp_send_request::from_native(udp_send.native_request()));
}
