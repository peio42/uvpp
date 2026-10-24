#include <type_traits>
#include "platform-support.hpp"

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

static_assert(uv::stream_handle<uv::tcp>);
static_assert(uv::stream_handle<uv::pipe>);
static_assert(uv::stream_handle<uv::tty>);
static_assert(!uv::stream_handle<uv::raw::timer>);

static_assert(uv::detail::native_handle_recoverable<uv::raw::timer>);
static_assert(!uv::detail::native_handle_recoverable<int>);

TEST(Uvpp2Layout, reconstructsHandlesFromNativePointers) {
  static_assert(std::is_standard_layout_v<uv::raw::async::native_storage>);
  static_assert(std::is_standard_layout_v<uv::raw::check::native_storage>);
  static_assert(std::is_standard_layout_v<uv::raw::fs_event::native_storage>);
  static_assert(std::is_standard_layout_v<uv::fs_poll::native_storage>);
  static_assert(std::is_standard_layout_v<uv::raw::idle::native_storage>);
  static_assert(std::is_standard_layout_v<uv::pipe::native_storage>);
  static_assert(std::is_standard_layout_v<uv::raw::poll::native_storage>);
  static_assert(std::is_standard_layout_v<uv::raw::prepare::native_storage>);
  static_assert(std::is_standard_layout_v<uv::raw::signal::native_storage>);
  static_assert(std::is_standard_layout_v<uv::raw::timer::native_storage>);
  static_assert(std::is_standard_layout_v<uv::tcp::native_storage>);
  static_assert(std::is_standard_layout_v<uv::tty::native_storage>);
  static_assert(std::is_standard_layout_v<uv::udp::native_storage>);

  uv::loop loop;
  uv::raw::async async(loop);
  uv::raw::check check(loop);
  uv::raw::fs_event fs_event(loop);
  uv::fs_poll fs_poll(loop);
  uv::raw::idle idle(loop);
  uv::pipe pipe(loop);
#ifdef _WIN32
  uv_os_sock_t fds[2]{};
  ASSERT_EQ(uv_socketpair(SOCK_STREAM, 0, fds, 0, 0), 0);
  uv::raw::poll poll(loop, fds[0], uv::socket_poll);
#else
  int fds[2]{};
  ASSERT_EQ(::pipe(fds), 0);
  uv::raw::poll poll(loop, fds[0]);
#endif
  uv::raw::prepare prepare(loop);
  uv::raw::signal signal(loop);
  uv::raw::timer timer(loop);
  uv::tcp tcp(loop);
  uv::udp udp(loop);

  EXPECT_EQ(&async, &uv::raw::async::from_native(async.native()));
  EXPECT_EQ(&async, &uv::raw::async::from_native(async.native_handle()));
  EXPECT_EQ(&check, &uv::raw::check::from_native(check.native()));
  EXPECT_EQ(&check, &uv::raw::check::from_native(check.native_handle()));
  EXPECT_EQ(&fs_event, &uv::raw::fs_event::from_native(fs_event.native()));
  EXPECT_EQ(&fs_event, &uv::raw::fs_event::from_native(fs_event.native_handle()));
  EXPECT_EQ(&fs_poll, &uv::fs_poll::from_native(fs_poll.native()));
  EXPECT_EQ(&fs_poll, &uv::fs_poll::from_native(fs_poll.native_handle()));
  EXPECT_EQ(&idle, &uv::raw::idle::from_native(idle.native()));
  EXPECT_EQ(&idle, &uv::raw::idle::from_native(idle.native_handle()));
  EXPECT_EQ(&pipe, &uv::pipe::from_native(pipe.native()));
  EXPECT_EQ(&pipe, &uv::pipe::from_native(pipe.native_handle()));
  EXPECT_EQ(&poll, &uv::raw::poll::from_native(poll.native()));
  EXPECT_EQ(&poll, &uv::raw::poll::from_native(poll.native_handle()));
  EXPECT_EQ(&prepare, &uv::raw::prepare::from_native(prepare.native()));
  EXPECT_EQ(&prepare, &uv::raw::prepare::from_native(prepare.native_handle()));
  EXPECT_EQ(&signal, &uv::raw::signal::from_native(signal.native()));
  EXPECT_EQ(&signal, &uv::raw::signal::from_native(signal.native_handle()));
  EXPECT_EQ(&timer, &uv::raw::timer::from_native(timer.native()));
  EXPECT_EQ(&timer, &uv::raw::timer::from_native(timer.native_handle()));
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
#ifdef _WIN32
  closesocket(fds[0]);
  closesocket(fds[1]);
#else
  close(fds[0]);
  close(fds[1]);
#endif
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
  static_assert(std::is_standard_layout_v<uv::getaddrinfo_request::native_storage>);
  static_assert(std::is_standard_layout_v<uv::getnameinfo_request::native_storage>);
  static_assert(std::is_standard_layout_v<uv::raw::fs::request::native_storage>);
#if UVPP_HAS_RANDOM
  static_assert(std::is_standard_layout_v<uv::random_request::native_storage>);
#endif
  static_assert(std::is_standard_layout_v<uv::shutdown_request::native_storage>);
  static_assert(std::is_standard_layout_v<uv::udp_send_request::native_storage>);
  static_assert(std::is_standard_layout_v<uv::work_request::native_storage>);

  uv::write_request write;
  uv::connect_request connect;
  uv::getaddrinfo_request getaddrinfo;
  uv::getnameinfo_request getnameinfo;
  uv::raw::fs::request fs;
#if UVPP_HAS_RANDOM
  uv::random_request random;
#endif
  uv::shutdown_request shutdown;
  uv::udp_send_request udp_send;
  uv::work_request work;

  EXPECT_EQ(&write, &uv::write_request::from_native(write.native()));
  EXPECT_EQ(&write, &uv::write_request::from_native(write.native_request()));
  EXPECT_EQ(&connect, &uv::connect_request::from_native(connect.native()));
  EXPECT_EQ(&connect, &uv::connect_request::from_native(connect.native_request()));
  EXPECT_EQ(&getaddrinfo, &uv::getaddrinfo_request::from_native(getaddrinfo.native()));
  EXPECT_EQ(&getaddrinfo, &uv::getaddrinfo_request::from_native(getaddrinfo.native_request()));
  EXPECT_EQ(&getnameinfo, &uv::getnameinfo_request::from_native(getnameinfo.native()));
  EXPECT_EQ(&getnameinfo, &uv::getnameinfo_request::from_native(getnameinfo.native_request()));
  EXPECT_EQ(&fs, &uv::raw::fs::request::from_native(fs.native()));
  EXPECT_EQ(&fs, &uv::raw::fs::request::from_native(fs.native_request()));
#if UVPP_HAS_RANDOM
  EXPECT_EQ(&random, &uv::random_request::from_native(random.native()));
  EXPECT_EQ(&random, &uv::random_request::from_native(random.native_request()));
#endif
  EXPECT_EQ(&shutdown, &uv::shutdown_request::from_native(shutdown.native()));
  EXPECT_EQ(&shutdown, &uv::shutdown_request::from_native(shutdown.native_request()));
  EXPECT_EQ(&udp_send, &uv::udp_send_request::from_native(udp_send.native()));
  EXPECT_EQ(&udp_send, &uv::udp_send_request::from_native(udp_send.native_request()));
  EXPECT_EQ(&work, &uv::work_request::from_native(work.native()));
  EXPECT_EQ(&work, &uv::work_request::from_native(work.native_request()));
}
