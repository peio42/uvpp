#include <memory>
#include <string>
#include <type_traits>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "gtest/gtest.h"
#include "platform-support.hpp"
#include "uvpp/handles/process.hpp"

namespace {
using child_type = uv::raw::process;
using options_type = uv::raw::process_options;

options_type child_options(int status) {
  return options_type::make(uvpp::test::executable_path())
    .args({"--uvpp-child", "exit", std::to_string(status)});
}
struct static_state { bool exited = false, closed = false; };
void static_closed(child_type &child) {
  child.user_data<static_state>()->closed = true;
}
void static_exit(child_type &child, uv::raw::process_exit result) {
  auto &state = *child.user_data<static_state>();
  EXPECT_EQ(result.status, 3);
  EXPECT_EQ(result.signal, 0);
  state.exited = true;
  child.close_static<static_closed>();
}
}

TEST(RawProcess, defaultHandleGuardsUninitializedOperations) {
  static_assert(!std::is_copy_constructible_v<child_type>);
  static_assert(!std::is_move_constructible_v<child_type>);
  child_type child;
  int marker = 42;
  child.user_data(marker);
  EXPECT_FALSE(child.initialized());
  EXPECT_FALSE(child.active());
  EXPECT_FALSE(child.closing());
  EXPECT_FALSE(child.has_ref());
  EXPECT_EQ(child.pid(), 0);
  EXPECT_EQ(child.kill(0).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(child.fileno().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(child.send_buffer_size().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(child.receive_buffer_size().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(child.send_buffer_size(1024).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(child.receive_buffer_size(1024).error(), uv::make_error_code(UV_EBADF));
  child.close();
  EXPECT_EQ(child.user_data<int>(), &marker);
}

TEST(RawProcess, failedNativeSpawnMustBeClosedAndCannotBeReinitialized) {
  uv::loop loop;
  child_type child;
  int marker = 7;
  child.user_data(marker);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  bool callback_called = false;
  auto missing = options_type::make("uvpp-nonexistent-process-6cb14a2e");
  const auto failure = child.spawn(loop, missing, [&, token](auto&, auto) { callback_called = true; });
  token.reset();
  EXPECT_EQ(failure.error(), uv::make_error_code(UV_ENOENT));
  EXPECT_TRUE(child.initialized());
  EXPECT_FALSE(child.active());
  EXPECT_FALSE(child.closing());
  EXPECT_EQ(child.user_data<int>(), &marker);
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(child.pid(), 0);
  EXPECT_EQ(child.kill(0).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(child.spawn(loop, child_options(0)).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(loop.handles().size(), 1u);
  bool closed = false;
  child.close([&](child_type &self) { EXPECT_EQ(self.user_data<int>(), &marker); closed = true; });
  EXPECT_TRUE(child.closing());
  EXPECT_EQ(child.kill(0).error(), uv::make_error_code(UV_EBADF));
  loop.run();
  EXPECT_TRUE(closed);
  EXPECT_FALSE(callback_called);
  EXPECT_TRUE(loop.handles().empty());
  EXPECT_EQ(child.spawn(loop, child_options(0)).error(), uv::make_error_code(UV_EALREADY));
  loop.close();
}

TEST(RawProcess, spawnStaticDeliversExitAndClose) {
  uv::loop loop;
  child_type child;
  static_state state;
  child.user_data(state);
  ASSERT_TRUE(child.spawn_static<static_exit>(loop, child_options(3)));
  EXPECT_TRUE(child.initialized());
  EXPECT_GT(child.pid(), 0);
  uv_os_fd_t native_fd{};
  const int fileno_status = uv_fileno(child.native_handle(), &native_fd);
  const auto descriptor = child.fileno();
  EXPECT_LT(fileno_status, 0);
  EXPECT_EQ(descriptor.error(), uv::make_error_code(fileno_status));
  int size = 0;
  const int send_status = uv_send_buffer_size(child.native_handle(), &size);
  const auto send_size = child.send_buffer_size();
  EXPECT_LT(send_status, 0);
  EXPECT_EQ(send_size.error(), uv::make_error_code(send_status));
  size = 0;
  const int receive_status = uv_recv_buffer_size(child.native_handle(), &size);
  const auto receive_size = child.receive_buffer_size();
  EXPECT_LT(receive_status, 0);
  EXPECT_EQ(receive_size.error(), uv::make_error_code(receive_status));
  EXPECT_EQ(child.spawn_static<static_exit>(loop, child_options(3)).error(), uv::make_error_code(UV_EALREADY));
  loop.run();
  EXPECT_TRUE(state.exited);
  EXPECT_TRUE(state.closed);
  loop.close();
}

TEST(RawProcess, runtimeCallbackRemainsAliveUntilExitThenIsReleased) {
  uv::loop loop;
  child_type child;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  bool exited = false, closed = false;
  ASSERT_TRUE(child.spawn(loop, child_options(7), [&, token](auto &self, auto result) {
    EXPECT_EQ(result.status, 7);
    EXPECT_EQ(result.signal, 0);
    EXPECT_FALSE(weak.expired());
    const int native_kill = uv_process_kill(self.native(), 0);
    EXPECT_EQ(self.kill(0).error(), uv::make_error_code(native_kill));
    exited = true;
    self.close([&](auto &) { closed = true; });
  }));
  token.reset();
  EXPECT_FALSE(weak.expired());
  loop.run();
  EXPECT_TRUE(exited);
  EXPECT_TRUE(closed);
  EXPECT_TRUE(weak.expired());
  loop.close();
}

TEST(RawProcess, emptyExitCallbackStillAllowsClosingAfterExit) {
  uv::loop loop;
  child_type child;
  ASSERT_TRUE(child.spawn(loop, child_options(0)));
  EXPECT_TRUE(child.active());
  loop.run();
  EXPECT_FALSE(child.active());
  bool closed = false;
  child.close([&](child_type &) { closed = true; });
  loop.run();
  EXPECT_TRUE(closed);
  loop.close();
}

TEST(RawProcess, globalKillReturnsNativeStatus) {
  const auto pid = uv_os_getpid();
  const int native_result = uv_kill(static_cast<int>(pid), 0);
  EXPECT_EQ(child_type::kill(pid, 0).error(), uv::make_error_code(native_result));
}

#if defined(__linux__)
// Passing an already-open pipe violates libuv's documented stdio precondition,
// but reliably exercises its late failure after the child has started.
TEST(RawProcess, lateStdioFailurePreservesActiveChildAndExitCallback) {
  uv::loop loop;
  uv_pipe_t pipe_handle{};
  ASSERT_EQ(uv_pipe_init(loop.native(), &pipe_handle, 0), 0);
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  ASSERT_EQ(uv_pipe_open(&pipe_handle, fds[0]), 0);
  close(fds[1]);
  uv_stdio_container_t io{};
  io.flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE);
  io.data.stream = reinterpret_cast<uv_stream_t *>(&pipe_handle);
  auto options = child_options(0).stdio({io});
  child_type child;
  bool exited = false, closed = false;
  const auto result = child.spawn(loop, options, [&](child_type &self, uv::raw::process_exit exit) {
    EXPECT_EQ(exit.status, 0);
    exited = true;
    self.close([&](child_type &) { closed = true; });
  });
  if (!child.active()) {
    child.close();
    uv_close(reinterpret_cast<uv_handle_t *>(&pipe_handle), nullptr);
    loop.run();
    loop.close();
    GTEST_SKIP() << "This libuv rejects the stdio setup before starting the child";
  }
  EXPECT_EQ(result.error(), uv::make_error_code(UV_EBUSY));
  EXPECT_TRUE(child.initialized());
  EXPECT_TRUE(child.active());
  EXPECT_GT(child.pid(), 0);
  const int native_kill = uv_process_kill(child.native(), 0);
  EXPECT_EQ(child.kill(0).error(), uv::make_error_code(native_kill));
  uv_close(reinterpret_cast<uv_handle_t *>(&pipe_handle), nullptr);
  loop.run();
  EXPECT_TRUE(exited);
  EXPECT_TRUE(closed);
  loop.close();
}

TEST(RawProcess, lateStdioFailurePreservesStaticExitCallback) {
  uv::loop loop;
  uv_pipe_t pipe_handle{};
  ASSERT_EQ(uv_pipe_init(loop.native(), &pipe_handle, 0), 0);
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  ASSERT_EQ(uv_pipe_open(&pipe_handle, fds[0]), 0);
  close(fds[1]);
  uv_stdio_container_t io{};
  io.flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE);
  io.data.stream = reinterpret_cast<uv_stream_t *>(&pipe_handle);
  auto options = child_options(3).stdio({io});
  child_type child;
  static_state state;
  child.user_data(state);
  const auto result = child.spawn_static<static_exit>(loop, options);
  if (!child.active()) {
    child.close();
    uv_close(reinterpret_cast<uv_handle_t *>(&pipe_handle), nullptr);
    loop.run();
    loop.close();
    GTEST_SKIP() << "This libuv rejects the stdio setup before starting the child";
  }
  EXPECT_EQ(result.error(), uv::make_error_code(UV_EBUSY));
  EXPECT_GT(child.pid(), 0);
  uv_close(reinterpret_cast<uv_handle_t *>(&pipe_handle), nullptr);
  loop.run();
  EXPECT_TRUE(state.exited);
  EXPECT_TRUE(state.closed);
  loop.close();
}

TEST(RawProcess, throwingConstructorRetainsLateFailedChildUntilExit) {
  uv::loop loop;
  uv_pipe_t pipe_handle{};
  ASSERT_EQ(uv_pipe_init(loop.native(), &pipe_handle, 0), 0);
  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  ASSERT_EQ(uv_pipe_open(&pipe_handle, fds[0]), 0);
  close(fds[1]);
  uv_stdio_container_t io{};
  io.flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_READABLE_PIPE);
  io.data.stream = reinterpret_cast<uv_stream_t *>(&pipe_handle);
  auto options = child_options(0).stdio({io});
  EXPECT_THROW((child_type{loop, options, [](child_type &, uv::raw::process_exit) { ADD_FAILURE(); }}), uv::error);
  const bool child_started = uv_loop_alive(loop.native()) != 0;
  uv_close(reinterpret_cast<uv_handle_t *>(&pipe_handle), nullptr);
  loop.run();
  EXPECT_TRUE(loop.handles().empty());
  EXPECT_NO_THROW(loop.close());
  if (!child_started) GTEST_SKIP() << "This libuv rejects the stdio setup before starting the child";
}
#endif
