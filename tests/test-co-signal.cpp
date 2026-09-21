#include "co-test-support.hpp"

#include <chrono>
#include <csignal>
#include <optional>

#include "uvpp/handles/timer.hpp"
#include "uvpp/signal_source.hpp"

using namespace std::chrono_literals;

TEST(UvppV3SignalSource, remainsSubscribedBetweenSuccessiveNextCalls) {
  uv::loop loop;
  uv::signal_source source(loop, SIGUSR1);
  uv::timer sender(loop);
  int sent = 0;
  std::optional<uv::signal_number> first;
  std::optional<uv::signal_number> second;

  sender.start(1ms, 1ms, [&](uv::timer &self) {
    ASSERT_EQ(::raise(SIGUSR1), 0);
    if (++sent == 2) {
      self.close();
    }
  });

  auto receive = [&]() -> uv::co::task<void> {
    first.emplace(co_await source.next());
    second.emplace(co_await source.next());
    co_await source.close();
  };

  auto execution = uv::co::spawn(loop, receive());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(first, SIGUSR1);
  EXPECT_EQ(second, SIGUSR1);
  EXPECT_EQ(sent, 2);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3SignalSource, coalescesNotificationReceivedWithoutAWaiter) {
  uv::loop loop;
  uv::signal_source source(loop, SIGUSR1);
  std::optional<uv::signal_number> received;

  ASSERT_EQ(::raise(SIGUSR1), 0);
  (void)loop.run(uv::run_mode::nowait);

  auto receive = [&]() -> uv::co::task<void> {
    received.emplace(co_await source.next());
    co_await source.close();
  };

  auto execution = uv::co::spawn(loop, receive());
  // Consuming a pending notification does not need another native callback;
  // this proves that the coroutine reached close rather than suspended in next.
  EXPECT_FALSE(execution.done());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(received, SIGUSR1);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3SignalSource, cancelledWaitLeavesTheSubscriptionUsable) {
  uv::loop loop;
  uv::signal_source source(loop, SIGUSR1);
  uv::timer canceller(loop);
  std::optional<uv::result<uv::signal_number>> cancelled;
  std::optional<uv::signal_number> received;

  auto wait_then_stop = [&]() -> uv::co::task<void> {
    cancelled.emplace(co_await uv::ops::next(source));
    loop.stop();
  };
  auto cancelled_execution = uv::co::spawn(loop, wait_then_stop());
  canceller.start(1ms, [&](uv::timer &self) {
    cancelled_execution.request_stop();
    self.close();
  });
  loop.run();

  ASSERT_TRUE(cancelled_execution.done());
  EXPECT_NO_THROW(cancelled_execution.rethrow_if_failed());
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_FALSE(*cancelled);
  EXPECT_EQ(cancelled->error(), uv::make_error_code(UV_ECANCELED));
  EXPECT_FALSE(source.closing());

  uv::timer sender(loop);
  sender.start(1ms, [&](uv::timer &self) {
    ASSERT_EQ(::raise(SIGUSR1), 0);
    self.close();
  });
  auto wait_again = [&]() -> uv::co::task<void> {
    received.emplace(co_await source.next());
    co_await source.close();
  };
  auto second_execution = uv::co::spawn(loop, wait_again());
  loop.run();

  EXPECT_TRUE(second_execution.done());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_EQ(received, SIGUSR1);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3SignalSource, rejectsConcurrentWaitersAndCloseCancelsTheActiveOne) {
  uv::loop loop;
  uv::signal_source source(loop, SIGUSR1);
  std::optional<uv::result<uv::signal_number>> first;
  std::optional<uv::result<uv::signal_number>> second;

  auto wait = [&]() -> uv::co::task<void> {
    first.emplace(co_await uv::ops::next(source));
  };
  auto close_after_busy = [&]() -> uv::co::task<void> {
    second.emplace(co_await uv::ops::next(source));
    co_await source.close();
  };

  auto first_execution = uv::co::spawn(loop, wait());
  auto second_execution = uv::co::spawn(loop, close_after_busy());
  loop.run();

  EXPECT_TRUE(first_execution.done());
  EXPECT_TRUE(second_execution.done());
  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_FALSE(*first);
  EXPECT_FALSE(*second);
  EXPECT_EQ(first->error(), uv::make_error_code(UV_ECANCELED));
  EXPECT_EQ(second->error(), uv::make_error_code(UV_EBUSY));
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3SignalSource, movePreservesTheNativeSubscriptionAddress) {
  uv::loop loop;
  uv::signal_source initial(loop, SIGUSR1);
  auto *native = initial.native();
  uv::signal_source source(std::move(initial));
  uv::timer sender(loop);
  std::optional<uv::signal_number> received;

  EXPECT_EQ(source.native(), native);
  EXPECT_EQ(initial.native(), nullptr);
  sender.start(1ms, [&](uv::timer &self) {
    ASSERT_EQ(::raise(SIGUSR1), 0);
    self.close();
  });
  auto receive = [&]() -> uv::co::task<void> {
    received.emplace(co_await source.next());
    co_await source.close();
  };

  auto execution = uv::co::spawn(loop, receive());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(received, SIGUSR1);
  EXPECT_NO_THROW(loop.close());
}
