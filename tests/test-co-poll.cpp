#include "gtest/gtest.h"
#include "uvpp/poll_source.hpp"
#include "uvpp/co/sleep.hpp"
#include "uvpp/handles/timer.hpp"

#include <optional>
#include <type_traits>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
struct socket_pair {
  int fd[2]{-1, -1};
  socket_pair() {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
      throw std::runtime_error{"socketpair failed"};
    }
  }
  ~socket_pair() {
    for (auto descriptor : fd) {
      if (descriptor >= 0) { ::close(descriptor); }
    }
  }
};

void expect_done(uv::co::spawn_handle<> &execution) {
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
}
} // namespace

static_assert(!std::is_copy_constructible_v<uv::poll_source>);
static_assert(std::is_nothrow_move_constructible_v<uv::poll_source>);

TEST(UvppV3PollSource, inactiveConstructionAndCloseBorrowDescriptor) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0]};
  EXPECT_EQ(uv_is_active(source.native_handle()), 0);
  EXPECT_NE(::fcntl(sockets.fd[0], F_GETFL) & O_NONBLOCK, 0);
  EXPECT_FALSE(loop.alive());
  auto close = [&]() -> uv::co::task<void> { co_await uv::ops::close(source); };
  auto execution = uv::co::spawn(loop, close());
  loop.run();
  expect_done(execution);
  EXPECT_NE(::fcntl(sockets.fd[0], F_GETFD), -1);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, successiveWaitsRearmReadinessAndChangeMask) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0], uv::socket_poll};
  ASSERT_EQ(::write(sockets.fd[1], "xy", 2), 2);
  int deliveries = 0;
  auto receive = [&]() -> uv::co::task<void> {
    auto first = co_await source.next(uv::poll_event::readable);
    EXPECT_TRUE(first.has(uv::poll_event::readable));
    EXPECT_EQ(uv_is_active(source.native_handle()), 0);
    ++deliveries;
    // Readiness remains present without consuming bytes. Rearming must detect
    // that same level, without an application queue or a new write.
    auto second = co_await uv::ops::next(source, uv::poll_event::readable);
    EXPECT_TRUE(second);
    EXPECT_TRUE(second.value().has(uv::poll_event::readable));
    ++deliveries;
    char bytes[2]{};
    EXPECT_EQ(::read(sockets.fd[0], bytes, 2), 2);
    auto writable = co_await source.next(uv::poll_event::writable);
    EXPECT_TRUE(writable.has(uv::poll_event::writable));
    ++deliveries;
    co_await source.close();
  };
  auto execution = uv::co::spawn(loop, receive());
  loop.run();
  expect_done(execution);
  EXPECT_EQ(deliveries, 3);
  EXPECT_NO_THROW(loop.close());
}


TEST(UvppV3PollSource, idleIntervalRetainsNoStaleReadiness) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0]};
  uv::timer sender{loop};
  ASSERT_EQ(::write(sockets.fd[1], "x", 1), 1);
  bool fresh_write = false;
  auto wait = [&]() -> uv::co::task<void> {
    (void)co_await source.next(uv::poll_event::readable);
    EXPECT_EQ(uv_is_active(source.native_handle()), 0);
    // Drive the loop while readiness remains present and no waiter is active.
    co_await uv::co::sleep_for(std::chrono::milliseconds{1});
    char byte{};
    EXPECT_EQ(::read(sockets.fd[0], &byte, 1), 1);
    sender.start(std::chrono::milliseconds{1}, [&](uv::timer &timer) {
      fresh_write = true;
      EXPECT_EQ(::write(sockets.fd[1], "y", 1), 1);
      timer.close();
    });
    (void)co_await source.next(uv::poll_event::readable);
    EXPECT_TRUE(fresh_write); // No cached notification from the idle interval.
    EXPECT_EQ(::read(sockets.fd[0], &byte, 1), 1);
    EXPECT_EQ(byte, 'y');
    co_await source.close();
  };
  auto execution = uv::co::spawn(loop, wait());
  loop.run();
  expect_done(execution);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, concurrentWaitDoesNotReplaceOriginalMask) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0]};
  bool readable = false;
  auto first = [&]() -> uv::co::task<void> {
    readable = (co_await source.next(uv::poll_event::readable)).has(uv::poll_event::readable);
    co_await source.close();
  };
  auto second = [&]() -> uv::co::task<void> {
    auto busy = co_await uv::ops::next(source, uv::poll_event::writable);
    EXPECT_EQ(busy.error(), uv::make_error_code(UV_EBUSY));
    try {
      (void)co_await source.next(uv::poll_event::writable);
      ADD_FAILURE() << "concurrent waiter must throw";
    } catch (const uv::error &error) {
      EXPECT_EQ(error.code().value(), UV_EBUSY);
    }
    EXPECT_EQ(::write(sockets.fd[1], "x", 1), 1);
  };
  auto a = uv::co::spawn(loop, first());
  auto b = uv::co::spawn(loop, second());
  loop.run();
  expect_done(a);
  expect_done(b);
  EXPECT_TRUE(readable);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, cancellationStopsWatcherAndAnotherTaskCanReuseSource) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0]};
  int cancelled = 0;
  auto wait = [&]() -> uv::co::task<void> {
    auto result = co_await uv::ops::next(source, uv::poll_event::readable);
    EXPECT_EQ(result.error(), uv::make_error_code(UV_ECANCELED));
    EXPECT_EQ(uv_is_active(source.native_handle()), 0);
    ++cancelled;
    try {
      (void)co_await source.next(uv::poll_event::readable);
      ADD_FAILURE() << "stopped context must throw";
    } catch (const uv::error &error) {
      EXPECT_EQ(error.code().value(), UV_ECANCELED);
      ++cancelled;
    }
  };
  auto a = uv::co::spawn(loop, wait());
  a.request_stop();
  expect_done(a);
  EXPECT_EQ(cancelled, 2);
  EXPECT_FALSE(source.closing());
  EXPECT_FALSE(loop.alive());
  auto reuse = [&]() -> uv::co::task<void> {
    auto event = co_await source.next(uv::poll_event::writable);
    EXPECT_TRUE(event.has(uv::poll_event::writable));
    co_await source.close();
  };
  auto b = uv::co::spawn(loop, reuse());
  loop.run();
  expect_done(b);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, closeCancelsWaiterBeforeNativeCloseAndJoinsRepeatedClose) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0]};
  int cancelled = 0;
  int closed = 0;
  auto wait = [&]() -> uv::co::task<void> {
    auto result = co_await uv::ops::next(source, uv::poll_event::readable);
    EXPECT_EQ(result.error(), uv::make_error_code(UV_ECANCELED));
    EXPECT_TRUE(source.closing());
    EXPECT_EQ(uv_is_active(source.native_handle()), 0);
    ++cancelled;
    auto invalid = co_await uv::ops::next(source, uv::poll_event::writable);
    EXPECT_EQ(invalid.error(), uv::make_error_code(UV_EBADF));
    co_await source.close();
    ++closed;
  };
  auto close = [&]() -> uv::co::task<void> {
    co_await source.close();
    ++closed;
    co_await source.close();
  };
  auto a = uv::co::spawn(loop, wait());
  auto b = uv::co::spawn(loop, close());
  b.request_stop(); // Close has already begun and is non-cancellable.
  EXPECT_EQ(cancelled, 1);
  EXPECT_EQ(closed, 0);
  loop.run();
  expect_done(a);
  expect_done(b);
  EXPECT_EQ(closed, 2);
  EXPECT_NE(::fcntl(sockets.fd[0], F_GETFD), -1);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, movingActiveOwnerPreservesWaitAndNativeAddress) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source original{loop, sockets.fd[0]};
  auto *native = original.native();
  auto wait = [&]() -> uv::co::task<void> {
    EXPECT_TRUE((co_await original.next(uv::poll_event::readable)).has(uv::poll_event::readable));
  };
  auto execution = uv::co::spawn(loop, wait());
  uv::poll_source moved{std::move(original)};
  EXPECT_EQ(moved.native(), native);
  EXPECT_EQ(original.native(), nullptr);
  EXPECT_EQ(native->data, moved.native()->data);
  auto moved_from = [&]() -> uv::co::task<void> {
    auto invalid = co_await uv::ops::next(original, uv::poll_event::readable);
    EXPECT_EQ(invalid.error(), uv::make_error_code(UV_EBADF));
    auto close_invalid = co_await uv::ops::close(original);
    EXPECT_EQ(close_invalid.error(), uv::make_error_code(UV_EBADF));
  };
  auto rejected = uv::co::spawn(loop, moved_from());
  expect_done(rejected);
  ASSERT_EQ(::write(sockets.fd[1], "x", 1), 1);
  loop.run();
  expect_done(execution);
  EXPECT_EQ(uv_is_active(moved.native_handle()), 0);
  moved.request_close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, deliveryCanDestroyOwnerAndStorageSurvivesClose) {
  uv::loop loop;
  socket_pair sockets;
  std::optional<uv::poll_source> source{std::in_place, loop, sockets.fd[0]};
  auto wait = [&]() -> uv::co::task<void> {
    EXPECT_TRUE((co_await source->next(uv::poll_event::readable)).has(uv::poll_event::readable));
    source.reset();
  };
  auto execution = uv::co::spawn(loop, wait());
  ASSERT_EQ(::write(sockets.fd[1], "x", 1), 1);
  loop.run();
  expect_done(execution);
  EXPECT_FALSE(source);
  EXPECT_NE(::fcntl(sockets.fd[0], F_GETFD), -1);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, destructionCancelsActiveWaitAndRetainsNativeStorage) {
  uv::loop loop;
  socket_pair sockets;
  std::optional<uv::poll_source> source{std::in_place, loop, sockets.fd[0]};
  int deliveries = 0;
  auto wait = [&]() -> uv::co::task<void> {
    auto cancelled = co_await uv::ops::next(*source, uv::poll_event::readable);
    EXPECT_EQ(cancelled.error(), uv::make_error_code(UV_ECANCELED));
    ++deliveries;
  };
  auto execution = uv::co::spawn(loop, wait());
  source.reset();
  expect_done(execution);
  EXPECT_EQ(deliveries, 1);
  EXPECT_EQ(loop.try_close(), uv::make_error_code(UV_EBUSY));
  loop.run();
  EXPECT_EQ(deliveries, 1);
  EXPECT_NO_THROW(loop.close());
}


TEST(UvppV3PollSource, closeCancellationCanReleaseOwnerBeforeNativeClose) {
  uv::loop loop;
  socket_pair sockets;
  std::optional<uv::poll_source> source{std::in_place, loop, sockets.fd[0]};
  auto wait = [&]() -> uv::co::task<void> {
    auto result = co_await uv::ops::next(*source, uv::poll_event::readable);
    EXPECT_EQ(result.error(), uv::make_error_code(UV_ECANCELED));
    source.reset();
  };
  auto close = [&]() -> uv::co::task<void> { co_await source->close(); };
  auto a = uv::co::spawn(loop, wait());
  auto b = uv::co::spawn(loop, close());
  EXPECT_FALSE(source);
  EXPECT_FALSE(b.done());
  loop.run();
  expect_done(a);
  expect_done(b);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, invalidMasksUseAwaitPolicyAndLeaveSourceReusable) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0]};
  auto wait = [&]() -> uv::co::task<void> {
    for (auto mask : {uv::poll_events{}, uv::poll_events::from_raw(0x100)}) {
      auto invalid = co_await uv::ops::next(source, mask);
      EXPECT_EQ(invalid.error(), uv::make_error_code(UV_EINVAL));
      try {
        (void)co_await source.next(mask);
        ADD_FAILURE() << "invalid mask must throw at await";
      } catch (const uv::error &error) {
        EXPECT_EQ(error.code().value(), UV_EINVAL);
      }
      EXPECT_EQ(uv_is_active(source.native_handle()), 0);
    }
    EXPECT_TRUE((co_await source.next(uv::poll_event::writable)).has(uv::poll_event::writable));
    co_await source.close();
  };
  auto execution = uv::co::spawn(loop, wait());
  loop.run();
  expect_done(execution);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, rejectsDifferentExecutionLoopBeforeArming) {
  uv::loop loop;
  uv::loop other;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0]};
  auto wait = [&]() -> uv::co::task<void> {
    try {
      (void)co_await uv::ops::next(source, uv::poll_event::readable);
      ADD_FAILURE() << "cross-loop next must reject";
    } catch (const std::logic_error &) {}
    try {
      co_await source.close();
      ADD_FAILURE() << "cross-loop close must reject";
    } catch (const std::logic_error &) {}
  };
  auto execution = uv::co::spawn(other, wait());
  expect_done(execution);
  EXPECT_EQ(uv_is_active(source.native_handle()), 0);
  source.request_close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
  EXPECT_NO_THROW(other.close());
}

TEST(UvppV3PollSource, failedInitializationLeavesNoNativeHandleToClose) {
  uv::loop loop;
  EXPECT_THROW((uv::poll_source{loop, -1}), uv::error);
  EXPECT_THROW((uv::poll_source{loop, static_cast<uv_os_sock_t>(-1), uv::socket_poll}), uv::error);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3PollSource, completionErrorStopsAndReleasesWaiterBeforeRetry) {
  uv::loop loop;
  socket_pair sockets;
  uv::poll_source source{loop, sockets.fd[0]};
  int deliveries = 0;
  auto wait = [&]() -> uv::co::task<void> {
    auto error = co_await uv::ops::next(source, uv::poll_event::readable);
    EXPECT_EQ(error.error(), uv::make_error_code(UV_EIO));
    ++deliveries;
    try {
      (void)co_await source.next(uv::poll_event::readable);
      ADD_FAILURE() << "native callback error must throw at await";
    } catch (const uv::error &error) {
      EXPECT_EQ(error.code().value(), UV_EIO);
    }
    ++deliveries;
    // A real native wait can immediately replace either failed operation.
    EXPECT_TRUE((co_await source.next(uv::poll_event::writable)).has(uv::poll_event::writable));
    ++deliveries;
    co_await source.close();
  };
  auto execution = uv::co::spawn(loop, wait());
  uv::detail::poll_source_state::on_poll(source.native(), UV_EIO, 0);
  uv::detail::poll_source_state::on_poll(source.native(), UV_EIO, 0);
  loop.run();
  expect_done(execution);
  EXPECT_EQ(deliveries, 3);
  EXPECT_NO_THROW(loop.close());
}


#if defined(__linux__)
TEST(UvppV3PollSource, nativeSubmissionConflictUsesBothErrorPolicies) {
  uv::loop loop;
  socket_pair sockets;
  // Two inactive handles may initialize. Once the first is active, libuv
  // rejects starting the other with UV_EEXIST; they never poll concurrently.
  uv::poll_source first{loop, sockets.fd[0]};
  uv::poll_source second{loop, sockets.fd[0]};
  auto wait = [&]() -> uv::co::task<void> {
    auto cancelled = co_await uv::ops::next(first, uv::poll_event::readable);
    EXPECT_EQ(cancelled.error(), uv::make_error_code(UV_ECANCELED));
  };
  auto conflict = [&]() -> uv::co::task<void> {
    auto error = co_await uv::ops::next(second, uv::poll_event::readable);
    EXPECT_EQ(error.error(), uv::make_error_code(UV_EEXIST));
    try {
      (void)co_await second.next(uv::poll_event::readable);
      ADD_FAILURE() << "native submission conflict must throw at await";
    } catch (const uv::error &error) {
      EXPECT_EQ(error.code().value(), UV_EEXIST);
    }
    EXPECT_EQ(uv_is_active(second.native_handle()), 0);
    co_await first.close();
    co_await second.close();
  };
  auto a = uv::co::spawn(loop, wait());
  auto b = uv::co::spawn(loop, conflict());
  loop.run();
  expect_done(a);
  expect_done(b);
  EXPECT_NO_THROW(loop.close());
}
#endif
#endif
