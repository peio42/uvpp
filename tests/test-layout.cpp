#include <type_traits>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Layout, reconstructsHandlesFromNativePointers) {
  static_assert(std::is_standard_layout_v<uv::async::native_storage>);
  static_assert(std::is_standard_layout_v<uv::check::native_storage>);
  static_assert(std::is_standard_layout_v<uv::fs_event::native_storage>);
  static_assert(std::is_standard_layout_v<uv::fs_poll::native_storage>);
  static_assert(std::is_standard_layout_v<uv::idle::native_storage>);
  static_assert(std::is_standard_layout_v<uv::pipe::native_storage>);
  static_assert(std::is_standard_layout_v<uv::poll::native_storage>);
  static_assert(std::is_standard_layout_v<uv::prepare::native_storage>);
  static_assert(std::is_standard_layout_v<uv::process::native_storage>);
  static_assert(std::is_standard_layout_v<uv::signal::native_storage>);
  static_assert(std::is_standard_layout_v<uv::timer::native_storage>);
  static_assert(std::is_standard_layout_v<uv::tcp::native_storage>);
  static_assert(std::is_standard_layout_v<uv::tty::native_storage>);
  static_assert(std::is_standard_layout_v<uv::udp::native_storage>);

  uv::loop loop;
  uv::async async(loop);
  uv::check check(loop);
  uv::fs_event fs_event(loop);
  uv::fs_poll fs_poll(loop);
  uv::idle idle(loop);
  uv::pipe pipe(loop);
  int fds[2]{};
  ASSERT_EQ(::pipe(fds), 0);
  uv::poll poll(loop, fds[0]);
  uv::prepare prepare(loop);
  uv::signal signal(loop);
  uv::timer timer(loop);
  uv::tcp tcp(loop);
  uv::udp udp(loop);

  EXPECT_EQ(&async, &uv::async::from_native(async.native()));
  EXPECT_EQ(&async, &uv::async::from_native(async.native_handle()));
  EXPECT_EQ(&check, &uv::check::from_native(check.native()));
  EXPECT_EQ(&check, &uv::check::from_native(check.native_handle()));
  EXPECT_EQ(&fs_event, &uv::fs_event::from_native(fs_event.native()));
  EXPECT_EQ(&fs_event, &uv::fs_event::from_native(fs_event.native_handle()));
  EXPECT_EQ(&fs_poll, &uv::fs_poll::from_native(fs_poll.native()));
  EXPECT_EQ(&fs_poll, &uv::fs_poll::from_native(fs_poll.native_handle()));
  EXPECT_EQ(&idle, &uv::idle::from_native(idle.native()));
  EXPECT_EQ(&idle, &uv::idle::from_native(idle.native_handle()));
  EXPECT_EQ(&pipe, &uv::pipe::from_native(pipe.native()));
  EXPECT_EQ(&pipe, &uv::pipe::from_native(pipe.native_handle()));
  EXPECT_EQ(&poll, &uv::poll::from_native(poll.native()));
  EXPECT_EQ(&poll, &uv::poll::from_native(poll.native_handle()));
  EXPECT_EQ(&prepare, &uv::prepare::from_native(prepare.native()));
  EXPECT_EQ(&prepare, &uv::prepare::from_native(prepare.native_handle()));
  EXPECT_EQ(&signal, &uv::signal::from_native(signal.native()));
  EXPECT_EQ(&signal, &uv::signal::from_native(signal.native_handle()));
  EXPECT_EQ(&timer, &uv::timer::from_native(timer.native()));
  EXPECT_EQ(&timer, &uv::timer::from_native(timer.native_handle()));
  EXPECT_EQ(&tcp, &uv::tcp::from_native(tcp.native()));
  EXPECT_EQ(&tcp, &uv::tcp::from_native(tcp.native_handle()));
  EXPECT_EQ(&udp, &uv::udp::from_native(udp.native()));
  EXPECT_EQ(&udp, &uv::udp::from_native(udp.native_handle()));

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

  uv::loop loop;
  uv::tty tty(loop, 1, false);

  EXPECT_EQ(&tty, &uv::tty::from_native(tty.native()));
  EXPECT_EQ(&tty, &uv::tty::from_native(tty.native_handle()));

  tty.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Layout, reconstructsRequestsFromNativePointers) {
  static_assert(std::is_standard_layout_v<uv::write_request::native_storage>);
  static_assert(std::is_standard_layout_v<uv::connect_request::native_storage>);
  static_assert(std::is_standard_layout_v<uv::fs::raw::request::native_storage>);
  static_assert(std::is_standard_layout_v<uv::shutdown_request::native_storage>);
  static_assert(std::is_standard_layout_v<uv::udp_send_request::native_storage>);

  uv::write_request write;
  uv::connect_request connect;
  uv::fs::raw::request fs;
  uv::shutdown_request shutdown;
  uv::udp_send_request udp_send;

  EXPECT_EQ(&write, &uv::write_request::from_native(write.native()));
  EXPECT_EQ(&write, &uv::write_request::from_native(write.native_request()));
  EXPECT_EQ(&connect, &uv::connect_request::from_native(connect.native()));
  EXPECT_EQ(&connect, &uv::connect_request::from_native(connect.native_request()));
  EXPECT_EQ(&fs, &uv::fs::raw::request::from_native(fs.native()));
  EXPECT_EQ(&fs, &uv::fs::raw::request::from_native(fs.native_request()));
  EXPECT_EQ(&shutdown, &uv::shutdown_request::from_native(shutdown.native()));
  EXPECT_EQ(&shutdown, &uv::shutdown_request::from_native(shutdown.native_request()));
  EXPECT_EQ(&udp_send, &uv::udp_send_request::from_native(udp_send.native()));
  EXPECT_EQ(&udp_send, &uv::udp_send_request::from_native(udp_send.native_request()));
}
