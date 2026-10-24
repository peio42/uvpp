#include <csignal>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <sys/socket.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

struct socket_pair {
  int fd[2]{-1, -1};
  socket_pair() {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) std::terminate();
  }
  ~socket_pair() { ::close(fd[0]); ::close(fd[1]); }
};

struct watched_directory {
  std::filesystem::path path = std::filesystem::temp_directory_path() /
    ("uvpp-raw-watchers-" + std::to_string(::getpid()));
  watched_directory() { std::filesystem::create_directories(path); }
  ~watched_directory() { std::filesystem::remove_all(path); }
};

template<class Watcher>
void check_stable_raw_type() {
  static_assert(!std::is_copy_constructible_v<Watcher>);
  static_assert(!std::is_move_constructible_v<Watcher>);
  static_assert(!std::is_convertible_v<Watcher&, uv_handle_t*>);
}

}

TEST(RawWatchers, normalizeCompletionStatusWithoutLosingPayload) {
  check_stable_raw_type<uv::raw::signal>();
  check_stable_raw_type<uv::raw::poll>();
  check_stable_raw_type<uv::raw::fs_event>();
  for (int code : {0, 7, static_cast<int>(UV_EIO)}) {
    uv::raw::poll_result poll{code, UV_READABLE};
    uv::raw::fs_event_result fs{"name", UV_CHANGE, code};
    const int normalized = code < 0 ? code : 0;
    EXPECT_EQ(poll.raw_status(), normalized);
    EXPECT_EQ(fs.raw_status(), normalized);
    EXPECT_EQ(poll.error_code(), uv::make_error_code(code));
    EXPECT_EQ(fs.error_code(), uv::make_error_code(code));
    EXPECT_EQ(poll.status().error(), poll.error_code());
    EXPECT_EQ(fs.status().error(), fs.error_code());
    EXPECT_TRUE(poll.has_event(uv::poll_event::readable));
    EXPECT_TRUE(fs.has(uv::raw::fs_event_kind::change));
    EXPECT_EQ(fs.filename(), "name");
  }
}

TEST(RawWatchers, signalFailurePreservesActiveCallbackAndStopReleasesIt) {
  uv::loop loop;
  uv::raw::signal watcher{uv::loop_view{loop.native()}};
  int data = 42;
  watcher.user_data(data);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  EXPECT_EQ(watcher.start(0, [token](auto&, auto) {}).error(), uv::make_error_code(UV_EINVAL));
  ASSERT_TRUE(watcher.start(SIGUSR1, [token](auto&, auto) {}));
  token.reset();
  auto rejected = std::make_shared<int>(2);
  std::weak_ptr<int> released = rejected;
  EXPECT_EQ(watcher.start_oneshot(0, [rejected](auto&, auto) {}).error(), uv::make_error_code(UV_EINVAL));
  rejected.reset();
  EXPECT_TRUE(released.expired());
  EXPECT_EQ(watcher.start_oneshot_static<+[](uv::raw::signal&, int) {}>(0).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(retained.expired());
  EXPECT_TRUE(watcher.active());
  EXPECT_EQ(watcher.user_data<int>(), &data);
  ASSERT_TRUE(watcher.stop());
  EXPECT_TRUE(retained.expired());
  watcher.close();
  EXPECT_EQ(watcher.stop().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(watcher.start_static<+[](uv::raw::signal&, int) {}>(SIGUSR1).error(),
            uv::make_error_code(UV_EBADF));
  loop.run();
  loop.close();
}

TEST(RawWatchers, signalOneShotQuiescesBeforeRuntimeAndStaticRearming) {
  uv::loop loop;
  uv::raw::signal watcher{loop};
  struct context { int calls = 0; std::weak_ptr<int> first; } context;
  watcher.user_data(context);
  auto token = std::make_shared<int>(1);
  context.first = token;
  constexpr auto second = +[](uv::raw::signal &self, uv::signal_number signum) {
    auto &state = *self.user_data<struct context>();
    EXPECT_EQ(++state.calls, 2);
    EXPECT_EQ(signum, SIGUSR1);
    EXPECT_FALSE(self.active());
    EXPECT_TRUE(state.first.expired());
    EXPECT_TRUE(self.start(SIGUSR1, [](uv::raw::signal &last, int) {
      EXPECT_EQ(++last.user_data<struct context>()->calls, 3);
      last.close();
    }));
    EXPECT_EQ(::raise(SIGUSR1), 0);
  };
  ASSERT_TRUE(watcher.start_oneshot(SIGUSR1, [&, token](auto &self, int) {
    EXPECT_EQ(++context.calls, 1);
    EXPECT_FALSE(self.active());
    EXPECT_TRUE(self.template start_oneshot_static<second>(SIGUSR1));
    EXPECT_EQ(::raise(SIGUSR1), 0);
  }));
  token.reset();
  ASSERT_EQ(::raise(SIGUSR1), 0);
  loop.run();
  EXPECT_EQ(context.calls, 3);
  loop.close();
}

TEST(RawWatchers, signalPersistentReplacementDoesNotRestoreOldCallable) {
  uv::loop loop;
  uv::raw::signal watcher{loop};
  int calls = 0;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(watcher.start(SIGUSR1, [&, token](auto &self, int) {
    ++calls;
    EXPECT_TRUE(self.stop());
    EXPECT_TRUE(self.start(SIGUSR1, [&](auto &next, int) {
      EXPECT_TRUE(old.expired());
      ++calls;
      next.close();
    }));
    EXPECT_EQ(::raise(SIGUSR1), 0);
  }));
  token.reset();
  ASSERT_EQ(::raise(SIGUSR1), 0);
  loop.run();
  EXPECT_EQ(calls, 2);
  loop.close();
}

TEST(RawWatchers, pollRejectsInvalidMasksAndZeroMaskReleasesCallback) {
  socket_pair sockets;
  uv::loop loop;
  uv::raw::poll watcher{loop, sockets.fd[0], uv::socket_poll};
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  ASSERT_TRUE(watcher.start(uv::poll_event::readable, [token](auto&, auto) {}));
  token.reset();
  EXPECT_EQ(watcher.start_raw(0x100, [](auto&, auto) {}).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(watcher.start_static_raw<+[](uv::raw::poll&, uv::raw::poll_result) {}>(0x100).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(watcher.active());
  EXPECT_FALSE(retained.expired());
  ASSERT_TRUE(watcher.start(uv::poll_events{}, [](auto&, auto) {}));
  EXPECT_FALSE(watcher.active());
  EXPECT_TRUE(retained.expired());
  watcher.close();
  EXPECT_EQ(watcher.stop().error(), uv::make_error_code(UV_EBADF));
  loop.run();
  loop.close();
}

TEST(RawWatchers, pollErrorQuiescesBeforeRearmingAndPreservesApplicationData) {
  socket_pair sockets;
  uv::loop loop;
  uv::raw::poll watcher{uv::loop_view{loop.native()}, sockets.fd[0]};
  int calls = 0;
  watcher.user_data(calls);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(watcher.start(uv::poll_event::readable, [&, token](auto &self, auto result) {
    EXPECT_FALSE(self.active());
    EXPECT_EQ(result.raw_status(), UV_EIO);
    EXPECT_TRUE(self.template start_static<+[](uv::raw::poll &last, uv::raw::poll_result ready) {
      EXPECT_TRUE(ready);
      ++*last.user_data<int>();
      last.close();
    }>(uv::poll_event::writable));
  }));
  token.reset();
  auto *native = watcher.native();
  native->poll_cb(native, UV_EIO, 0); // Inject native terminal failure at delivery.
  EXPECT_TRUE(old.expired());
  EXPECT_EQ(watcher.native(), native);
  EXPECT_EQ(watcher.user_data<int>(), &calls);
  loop.run();
  EXPECT_EQ(calls, 1);
  loop.close();
}

TEST(RawWatchers, pollStaticErrorCanRearmRuntimeCallback) {
  socket_pair sockets;
  uv::loop loop;
  uv::raw::poll watcher{loop, sockets.fd[0]};
  int calls = 0;
  watcher.user_data(calls);
  constexpr auto callback = +[](uv::raw::poll &self, uv::raw::poll_result result) {
    EXPECT_EQ(result.raw_status(), UV_EIO);
    EXPECT_FALSE(self.active());
    EXPECT_TRUE(self.start(uv::poll_event::writable, [](auto &last, auto) {
      ++*last.template user_data<int>();
      last.close();
    }));
  };
  ASSERT_TRUE(watcher.start_static<callback>(uv::poll_event::readable));
  watcher.native()->poll_cb(watcher.native(), UV_EIO, 0);
  loop.run();
  EXPECT_EQ(calls, 1);
  loop.close();
}

#ifdef __linux__
TEST(RawWatchers, pollNativeRejectionReleasesNewCallbackAndAllowsRetry) {
  socket_pair sockets;
  uv::loop loop;
  uv::raw::poll first{loop, sockets.fd[0]};
  uv::raw::poll second{loop, sockets.fd[0]};
  ASSERT_TRUE(first.start(uv::poll_event::readable, [](auto&, auto) {}));
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> rejected = token;
  EXPECT_EQ(second.start(uv::poll_event::readable, [token](auto&, auto) {}).error(),
            uv::make_error_code(UV_EEXIST));
  token.reset();
  EXPECT_TRUE(rejected.expired());
  EXPECT_EQ(second.start_static<+[](uv::raw::poll&, uv::raw::poll_result) {}>(uv::poll_event::readable).error(),
            uv::make_error_code(UV_EEXIST));
  ASSERT_TRUE(first.stop());
  ASSERT_TRUE(second.start(uv::poll_event::writable, [](auto &self, auto) { self.close(); }));
  first.close();
  loop.run();
  loop.close();
}
#endif

TEST(RawWatchers, fsEventRejectedStartsPreserveCallbackAndPathReportsErrors) {
  watched_directory dir;
  uv::loop loop;
  uv::raw::fs_event watcher{loop};
  EXPECT_FALSE(watcher.path());
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  EXPECT_FALSE(watcher.start((dir.path / "missing").string(), [token](auto&, auto) {}));
  ASSERT_TRUE(watcher.start(dir.path.string(), [token](auto&, auto) {}));
  token.reset();
  EXPECT_EQ(watcher.path().value(), dir.path.string());
  EXPECT_EQ(watcher.start(std::string_view{"bad\0path", 8}, [](auto&, auto) {}).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(watcher.start(dir.path.string(), [](auto&, auto) {}));
  EXPECT_FALSE(watcher.start_static<+[](uv::raw::fs_event&, uv::raw::fs_event_result) {}>(dir.path.string()));
  EXPECT_FALSE(retained.expired());
  EXPECT_TRUE(watcher.active());
  ASSERT_TRUE(watcher.stop());
  EXPECT_TRUE(retained.expired());
  watcher.close();
  EXPECT_EQ(watcher.path().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(watcher.start(dir.path.string(), [](auto&, auto) {}).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(watcher.stop().error(), uv::make_error_code(UV_EBADF));
  loop.run();
  loop.close();
}

#ifndef _WIN32
TEST(RawWatchers, fsEventTerminalErrorRearmsStaticWithBorrowedFilename) {
  watched_directory dir;
  uv::loop loop;
  uv::raw::fs_event watcher{uv::loop_view{loop.native()}};
  struct state { std::string path; int calls = 0; } state{dir.path.string()};
  watcher.user_data(state);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(watcher.start(state.path, [token](auto &self, auto error) {
    EXPECT_FALSE(self.active());
    EXPECT_EQ(error.raw_status(), UV_EIO);
    constexpr auto callback = +[](uv::raw::fs_event &last, uv::raw::fs_event_result result) {
      EXPECT_TRUE(result);
      EXPECT_EQ(result.filename(), "borrowed");
      ++last.user_data<struct state>()->calls;
      last.close();
    };
    EXPECT_TRUE(self.template start_static<callback>(self.template user_data<struct state>()->path));
  }));
  token.reset();
  auto *native = watcher.native();
  native->cb(native, nullptr, 0, UV_EIO);
  EXPECT_TRUE(old.expired());
  char filename[] = "borrowed";
  native->cb(native, filename, UV_CHANGE, 0);
  EXPECT_EQ(state.calls, 1);
  loop.run();
  loop.close();
}

TEST(RawWatchers, fsEventStaticErrorReleasesSlotBeforeRuntimeRestart) {
  watched_directory dir;
  uv::loop loop;
  uv::raw::fs_event watcher{loop};
  auto path = dir.path.string();
  watcher.user_data(path);
  constexpr auto callback = +[](uv::raw::fs_event &self, uv::raw::fs_event_result error) {
    EXPECT_FALSE(self.active());
    EXPECT_EQ(error.error_code(), uv::make_error_code(UV_EIO));
    EXPECT_TRUE(self.start(*self.user_data<std::string>(), [](auto &last, auto) { last.close(); }));
  };
  ASSERT_TRUE(watcher.start_static<callback>(path));
  watcher.native()->cb(watcher.native(), nullptr, 0, UV_EIO);
  watcher.native()->cb(watcher.native(), nullptr, UV_RENAME, 0);
  loop.run();
  loop.close();
}
#endif

TEST(RawWatchers, closeQuiescesAndReleasesCapturesBeforeCloseCompletion) {
  uv::loop loop;
  uv::raw::signal watcher{loop};
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  ASSERT_TRUE(watcher.start(SIGUSR1, [token](auto&, auto) {}));
  token.reset();
  bool closed = false;
  watcher.close([&](auto &self) { closed = true; EXPECT_FALSE(self.active()); });
  EXPECT_FALSE(watcher.active());
  EXPECT_TRUE(retained.expired());
  EXPECT_FALSE(closed);
  loop.run();
  EXPECT_TRUE(closed);
  loop.close();
}

TEST(RawWatchers, commonNativeQueriesReturnExplicitResults) {
  uv::loop loop;
  uv::raw::signal watcher{loop};
  uv_os_fd_t fd{};
  const auto native_fd_status = uv_fileno(watcher.native_handle(), &fd);
  auto descriptor = watcher.fileno();
  EXPECT_EQ(descriptor.error(), uv::make_error_code(native_fd_status));
  if (descriptor) {
    EXPECT_EQ(descriptor.value(), fd);
  }
  int size = 0;
  const auto native_send_status = uv_send_buffer_size(watcher.native_handle(), &size);
  auto send = watcher.send_buffer_size();
  EXPECT_EQ(send.error(), uv::make_error_code(native_send_status));
  const auto native_receive_status = uv_recv_buffer_size(watcher.native_handle(), &size);
  auto receive = watcher.receive_buffer_size();
  EXPECT_EQ(receive.error(), uv::make_error_code(native_receive_status));
  EXPECT_FALSE(watcher.send_buffer_size(4096));
  EXPECT_FALSE(watcher.receive_buffer_size(4096));
  watcher.close_static<+[](uv::raw::signal&) {}>();
  loop.run();
  loop.close();
}
