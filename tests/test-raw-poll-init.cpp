#include <chrono>
#include <memory>
#include <thread>
#include <stdexcept>
#include <type_traits>

#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "gtest/gtest.h"
#include "uvpp/handles/poll.hpp"
#include "uvpp/handles/timer.hpp"

namespace {

struct socket_pair {
#ifdef _WIN32
  uv_os_sock_t fd[2]{INVALID_SOCKET, INVALID_SOCKET};
#else
  uv_os_sock_t fd[2]{-1, -1};
#endif
  socket_pair() {
#ifdef _WIN32
    if (uv_socketpair(SOCK_STREAM, 0, fd, 0, 0) != 0)
#else
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0)
#endif
      throw std::runtime_error{"socketpair failed"};
  }
  ~socket_pair() {
    for (auto socket : fd) {
#ifdef _WIN32
      ::closesocket(socket);
#else
      ::close(socket);
#endif
    }
  }
};

void unexpected_poll(uv::raw::poll&, uv::raw::poll_result) { ADD_FAILURE(); }
void count_and_stop(uv::raw::poll &self, uv::raw::poll_result event) {
  EXPECT_TRUE(event);
  ++*self.user_data<int>();
  EXPECT_TRUE(self.stop());
}
void observe_close(uv::raw::poll &self) {
  EXPECT_TRUE(self.initialized());
  ++*self.user_data<int>();
}

class RawPollInit : public ::testing::TestWithParam<bool> {
protected:
  uv::loop loop;
  socket_pair sockets;
  uv::raw::poll watcher;

  ~RawPollInit() override {
    if (watcher.initialized() && !watcher.closing()) watcher.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }

  uv::status init(uv::loop &target) {
#ifdef _WIN32
    return watcher.init(target, sockets.fd[0], uv::socket_poll);
#else
    if (GetParam()) return watcher.init(target, sockets.fd[0], uv::socket_poll);
    return watcher.init(target, sockets.fd[0]);
#endif
  }
  uv::status init(uv::loop_view target) {
#ifdef _WIN32
    return watcher.init(target, sockets.fd[0], uv::socket_poll);
#else
    if (GetParam()) return watcher.init(target, sockets.fd[0], uv::socket_poll);
    return watcher.init(target, sockets.fd[0]);
#endif
  }
};

#ifdef _WIN32
INSTANTIATE_TEST_SUITE_P(Socket, RawPollInit, ::testing::Values(true));
#else
INSTANTIATE_TEST_SUITE_P(DescriptorAndSocket, RawPollInit, ::testing::Values(false, true));
#endif

TEST_P(RawPollInit, defaultStorageAndOperationsBeforeInit) {
  static_assert(std::is_nothrow_default_constructible_v<uv::raw::poll>);
  static_assert(!std::is_copy_constructible_v<uv::raw::poll>);
  static_assert(!std::is_move_constructible_v<uv::raw::poll>);
  static_assert(noexcept(watcher.init(loop, -1)));
  static_assert(noexcept(watcher.init(loop.view(), -1)));
  static_assert(noexcept(watcher.init(loop, sockets.fd[0], uv::socket_poll)));
  static_assert(noexcept(watcher.init(loop.view(), sockets.fd[0], uv::socket_poll)));
  EXPECT_FALSE(watcher.initialized());
  EXPECT_EQ(watcher.native_handle()->loop, nullptr);
  EXPECT_FALSE(watcher.active());
  EXPECT_FALSE(watcher.closing());
  watcher.ref();
  watcher.unref();
  EXPECT_FALSE(watcher.has_ref());
  const auto badf = uv::make_error_code(UV_EBADF);
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(watcher.start(uv::poll_event::readable, [token](auto&, auto) {}).error(), badf);
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(watcher.start(uv::poll_events{}, unexpected_poll).error(), badf);
  EXPECT_EQ(watcher.start_raw(0x100, unexpected_poll).error(), badf);
  EXPECT_EQ(watcher.start_static<unexpected_poll>(uv::poll_event::readable).error(), badf);
  EXPECT_EQ(watcher.start_static<unexpected_poll>(uv::poll_events{}).error(), badf);
  EXPECT_EQ(watcher.start_static_raw<unexpected_poll>(0x100).error(), badf);
  EXPECT_EQ(watcher.stop().error(), badf);
  EXPECT_EQ(watcher.fileno().error(), badf);
  EXPECT_EQ(watcher.send_buffer_size().error(), badf);
  EXPECT_EQ(watcher.send_buffer_size(1024).error(), badf);
  EXPECT_EQ(watcher.receive_buffer_size().error(), badf);
  EXPECT_EQ(watcher.receive_buffer_size(1024).error(), badf);
  int handles = 0;
  loop.walk([&](uv::handle_view) { ++handles; });
  EXPECT_EQ(handles, 0);
}

TEST_P(RawPollInit, preInitCloseIsInertAndInitPreservesNativeIdentity) {
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  watcher.close();
  watcher.close([token](auto&) { ADD_FAILURE(); });
  token.reset();
  EXPECT_TRUE(weak.expired());
  watcher.close_static<observe_close>();
  loop.run();
  EXPECT_FALSE(watcher.initialized());
  auto *native = watcher.native();
  int marker = 42;
  watcher.user_data(marker);
  ASSERT_TRUE(init(loop));
  EXPECT_TRUE(watcher.initialized());
  EXPECT_FALSE(watcher.active());
  EXPECT_TRUE(watcher.has_ref());
  EXPECT_EQ(watcher.native(), native);
  EXPECT_EQ(&uv::raw::poll::from_native(native), &watcher);
  EXPECT_EQ(watcher.user_data<int>(), &marker);
  EXPECT_EQ(watcher.native_handle()->loop, loop.native());
  EXPECT_TRUE(watcher.fileno());
  int handles = 0;
  loop.walk([&](uv::handle_view h) {
    ++handles;
    EXPECT_EQ(h.native_handle(), watcher.native_handle());
  });
  EXPECT_EQ(handles, 1);
}

TEST_P(RawPollInit, nativeFailureLeavesNoRegistrationAndAllowsRetry) {
  const auto invalid = static_cast<uv_os_sock_t>(-1);
  int marker = 42;
  watcher.user_data(marker);
  auto *native = watcher.native();
  const auto failed = watcher.init(loop.view(), invalid, uv::socket_poll);
  EXPECT_FALSE(failed);
  EXPECT_FALSE(watcher.initialized());
  EXPECT_EQ(watcher.native_handle()->loop, nullptr);
  EXPECT_FALSE(watcher.init(loop, invalid, uv::socket_poll));
  EXPECT_EQ(watcher.init(loop, -1).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(watcher.init(loop.view(), -1).error(), uv::make_error_code(UV_EBADF));
  int handles = 0;
  loop.walk([&](uv::handle_view) { ++handles; });
  EXPECT_EQ(handles, 0);
  watcher.close();
  ASSERT_TRUE(init(loop.view()));
  EXPECT_EQ(watcher.native(), native);
  EXPECT_EQ(watcher.user_data<int>(), &marker);
  EXPECT_FALSE(watcher.start_raw(0x100, unexpected_poll));
  EXPECT_TRUE(watcher.initialized());
  int called = 0;
  watcher.user_data(called);
  ASSERT_TRUE(watcher.start_static<count_and_stop>(uv::poll_event::writable));
  watcher.native()->poll_cb(watcher.native(), 0, UV_WRITABLE);
  EXPECT_EQ(called, 1);
}

TEST_P(RawPollInit, duplicateInitPreservesSubscriptionDescriptorAndOriginalLoop) {
  uv::loop other;
  ASSERT_TRUE(init(loop));
  int calls = 0;
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(watcher.start(uv::poll_event::readable, [&, token](auto &self, auto result) {
    ++calls;
    EXPECT_TRUE(result);
    EXPECT_EQ(*token, 42);
    EXPECT_EQ(init(loop).error(), uv::make_error_code(UV_EALREADY));
    EXPECT_TRUE(self.stop());
  }));
  token.reset();
  const auto fd = watcher.fileno().value();
  EXPECT_EQ(init(other.view()).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(watcher.init(other, -1).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(watcher.init(other, sockets.fd[1], uv::socket_poll).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(watcher.native_handle()->loop, loop.native());
  EXPECT_EQ(watcher.fileno().value(), fd);
  EXPECT_TRUE(watcher.active());
  EXPECT_FALSE(weak.expired());
  EXPECT_NO_THROW(other.close());
  watcher.native()->poll_cb(watcher.native(), 0, UV_READABLE);
  EXPECT_EQ(calls, 1);
  EXPECT_TRUE(weak.expired());
}

TEST_P(RawPollInit, closeCompletionRetainsSentinelAndBorrowedSocket) {
  ASSERT_TRUE(init(loop.view()));
  int closed = 0;
  watcher.user_data(closed);
  watcher.close_static<observe_close>();
  EXPECT_TRUE(watcher.initialized());
  EXPECT_TRUE(watcher.closing());
  EXPECT_EQ(init(loop).error(), uv::make_error_code(UV_EALREADY));
  loop.run();
  EXPECT_EQ(closed, 1);
  EXPECT_TRUE(watcher.initialized());
  EXPECT_EQ(init(loop.view()).error(), uv::make_error_code(UV_EALREADY));
  uv::raw::poll borrowing_again;
  ASSERT_TRUE(borrowing_again.init(loop, sockets.fd[0], uv::socket_poll));
  borrowing_again.close();
  loop.run();
}

TEST_P(RawPollInit, initializedSocketDeliversNativeReadiness) {
  ASSERT_TRUE(init(loop.view()));
  int calls = 0;
  ASSERT_TRUE(watcher.start(uv::poll_event::writable, [&](auto &self, auto result) {
    ++calls;
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.has_event(uv::poll_event::writable));
    EXPECT_TRUE(self.stop());
  }));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (calls == 0 && std::chrono::steady_clock::now() < deadline) {
    loop.run(uv::run_mode::nowait);
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  EXPECT_EQ(calls, 1);
}

TEST_P(RawPollInit, throwingConstructorsUseSameProtocolAndCleanupFailures) {
  const auto invalid = static_cast<uv_os_sock_t>(-1);
  EXPECT_THROW((uv::raw::poll{loop, -1}), uv::error);
  EXPECT_THROW((uv::raw::poll{loop.view(), -1}), uv::error);
  EXPECT_THROW((uv::raw::poll{loop, invalid, uv::socket_poll}), uv::error);
  EXPECT_THROW((uv::raw::poll{loop.view(), invalid, uv::socket_poll}), uv::error);
  // Keep simultaneous handles on distinct sockets; neither starts native polling.
  if (GetParam()) {
    uv::raw::poll owned{loop, sockets.fd[0], uv::socket_poll};
    uv::raw::poll viewed{loop.view(), sockets.fd[1], uv::socket_poll};
    EXPECT_TRUE(owned.initialized());
    EXPECT_TRUE(viewed.initialized());
    owned.close();
    viewed.close();
    loop.run();
  }
#ifndef _WIN32
  else {
    uv::raw::poll owned{loop, sockets.fd[0]};
    uv::raw::poll viewed{loop.view(), sockets.fd[1]};
    EXPECT_TRUE(owned.initialized());
    EXPECT_TRUE(viewed.initialized());
    owned.close();
    viewed.close();
    loop.run();
  }
#endif
}

TEST_P(RawPollInit, lateNativeFailureRollsBackOnlyUncommittedRegistration) {
  uv::raw::timer before{loop};
  uv::raw::timer after;
  int marker = 42;
  watcher.user_data(marker);
  auto *native = watcher.native();
  // Model Windows' post-registration failure on a genuine, inactive native
  // registration. No wrapper init has committed, and no poll request is started.
  ASSERT_EQ(uv_poll_init_socket(loop.native(), native, sockets.fd[0]), 0);
  ASSERT_TRUE(after.init(loop));
  EXPECT_EQ(uv::detail::finish_poll_init(*native, UV_EIO), UV_EIO);
  EXPECT_FALSE(watcher.initialized());
  EXPECT_EQ(watcher.user_data<int>(), &marker);
  int handles = 0;
  loop.walk([&](uv::handle_view h) {
    ++handles;
    EXPECT_NE(h.native_handle(), watcher.native_handle());
  });
  EXPECT_EQ(handles, 2);
  EXPECT_FALSE(loop.alive());
  ASSERT_TRUE(init(loop));
  EXPECT_EQ(watcher.native(), native);
  before.close();
  after.close();
  watcher.close();
  loop.run();
  EXPECT_FALSE(loop.alive());
}

} // namespace
