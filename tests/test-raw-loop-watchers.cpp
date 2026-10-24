#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/handles/check.hpp"
#include "uvpp/handles/idle.hpp"
#include "uvpp/handles/prepare.hpp"
#include "uvpp/handles/timer.hpp"

using namespace std::chrono_literals;

namespace {

template<class Watcher>
void count_and_stop_phase(Watcher &self) {
  ++*self.template user_data<int>();
  EXPECT_TRUE(self.stop());
}

template<class Watcher>
void unexpected_static_phase(Watcher&) {
  ADD_FAILURE() << "Unexpected static phase callback";
}

template<class Watcher>
class RawPhaseWatcher : public ::testing::Test {
protected:
  uv::loop loop;
  Watcher watcher{loop.view()};

  ~RawPhaseWatcher() override {
    if (!watcher.closing()) watcher.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }

  void tick() { loop.run(uv::run_mode::nowait); }
};

using phase_watchers = ::testing::Types<uv::raw::idle, uv::raw::prepare, uv::raw::check>;
TYPED_TEST_SUITE(RawPhaseWatcher, phase_watchers);

TYPED_TEST(RawPhaseWatcher, persistentCallbackPreservesStateAndNativeIdentity) {
  static_assert(!std::is_copy_constructible_v<TypeParam>);
  static_assert(!std::is_move_constructible_v<TypeParam>);
  static_assert(!std::is_convertible_v<TypeParam&, uv_handle_t*>);
  int marker = 42;
  auto *native = this->watcher.native();
  this->watcher.user_data(marker);
  int observed = 0;
  ASSERT_TRUE(this->watcher.start([&, count = 0](TypeParam &self) mutable {
    observed = ++count;
    EXPECT_EQ(self.native(), native);
    EXPECT_EQ(&self, &TypeParam::from_native(native));
    EXPECT_EQ(self.template user_data<int>(), &marker);
  }));
  this->tick();
  EXPECT_EQ(observed, 1);
  this->tick();
  EXPECT_EQ(observed, 2);
  ASSERT_TRUE(this->watcher.stop());
  this->tick();
  EXPECT_EQ(observed, 2);
}

TYPED_TEST(RawPhaseWatcher, activeRuntimeToStaticReplacementReleasesOldCapture) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(this->watcher.start([token](TypeParam&) { ADD_FAILURE(); }));
  token.reset();
  int called = 0;
  this->watcher.user_data(called);
  ASSERT_TRUE(this->watcher.template start_static<count_and_stop_phase<TypeParam>>());
  EXPECT_TRUE(old.expired());
  this->tick();
  EXPECT_EQ(called, 1);
  EXPECT_FALSE(this->watcher.active());
}

TYPED_TEST(RawPhaseWatcher, activeStaticToRuntimeReplacementUsesNewCallable) {
  ASSERT_TRUE(this->watcher.template start_static<unexpected_static_phase<TypeParam>>());
  int called = 0;
  ASSERT_TRUE(this->watcher.start([&](TypeParam &self) {
    ++called;
    EXPECT_TRUE(self.stop());
  }));
  this->tick();
  EXPECT_EQ(called, 1);
}

TYPED_TEST(RawPhaseWatcher, reentrantReplacementDoesNotRestoreExecutingCallable) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> old = token;
  int called = 0;
  this->watcher.user_data(called);
  ASSERT_TRUE(this->watcher.start([&, token](TypeParam &self) {
    EXPECT_EQ(*token, 1);
    EXPECT_TRUE(self.template start_static<count_and_stop_phase<TypeParam>>());
    EXPECT_FALSE(old.expired()); // The executing callable must still be alive.
  }));
  token.reset();
  this->tick();
  EXPECT_TRUE(old.expired());
  this->tick();
  EXPECT_EQ(called, 1);
}

TYPED_TEST(RawPhaseWatcher, stopReleasesCaptureAndAllowsFreshStart) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  ASSERT_TRUE(this->watcher.start([token](TypeParam&) {}));
  token.reset();
  ASSERT_TRUE(this->watcher.stop());
  EXPECT_TRUE(retained.expired());
  EXPECT_FALSE(this->watcher.active());
  int calls = 0;
  ASSERT_TRUE(this->watcher.start([&](TypeParam &self) { ++calls; EXPECT_TRUE(self.stop()); }));
  this->tick();
  EXPECT_EQ(calls, 1);
}

TYPED_TEST(RawPhaseWatcher, closeQuiescesReleasesCaptureAndRejectsRestart) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  ASSERT_TRUE(this->watcher.start([token](TypeParam&) { ADD_FAILURE(); }));
  token.reset();
  bool closed = false;
  this->watcher.close([&](TypeParam &self) { closed = true; EXPECT_FALSE(self.active()); });
  EXPECT_TRUE(retained.expired());
  EXPECT_FALSE(closed);
  EXPECT_FALSE(this->watcher.active());
  auto rejected = std::make_shared<int>(2);
  std::weak_ptr<int> released = rejected;
  EXPECT_EQ(this->watcher.start([rejected](TypeParam&) {}).error(), uv::make_error_code(UV_EBADF));
  rejected.reset();
  EXPECT_TRUE(released.expired());
  EXPECT_EQ(this->watcher.template start_static<unexpected_static_phase<TypeParam>>().error(),
            uv::make_error_code(UV_EBADF));
  EXPECT_EQ(this->watcher.stop().error(), uv::make_error_code(UV_EBADF));
  this->tick();
  EXPECT_TRUE(closed);
}

class RawTimer : public ::testing::Test {
protected:
  uv::loop loop;
  uv::raw::timer timer{loop.view()};
  ~RawTimer() override {
    if (!timer.closing()) timer.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }
};

TEST_F(RawTimer, oneShotCompletionReleasesCaptureWithoutExplicitStop) {
  static_assert(!std::is_move_constructible_v<uv::raw::timer>);
  static_assert(!std::is_copy_constructible_v<uv::raw::timer>);
  int marker = 42;
  timer.user_data(marker);
  auto *native = timer.native();
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  bool called = false;
  ASSERT_TRUE(timer.start(0ms, [&, token](auto &self) {
    EXPECT_FALSE(self.active());
    EXPECT_EQ(self.native(), native);
    EXPECT_EQ(self.template user_data<int>(), &marker);
    EXPECT_EQ(self.again().error(), uv::make_error_code(UV_EINVAL));
    called = true;
  }));
  token.reset();
  loop.run();
  EXPECT_TRUE(called);
  EXPECT_TRUE(retained.expired());
}

void on_rearmed_static_timer(uv::raw::timer &self) {
  ++*self.user_data<int>();
  EXPECT_FALSE(self.active());
  EXPECT_TRUE(self.start(0ms, [](uv::raw::timer &last) {
    ++*last.user_data<int>();
    last.close();
  }));
}

TEST_F(RawTimer, oneShotCanRearmStaticThenRuntimeFromDelivery) {
  int calls = 0;
  timer.user_data(calls);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(timer.start(0ms, [&, token](uv::raw::timer &self) {
    ++calls;
    EXPECT_TRUE(self.start_static<on_rearmed_static_timer>(0ms));
    EXPECT_EQ(*token, 1);
  }));
  token.reset();
  loop.run();
  EXPECT_EQ(calls, 3);
  EXPECT_TRUE(old.expired());
}

TEST_F(RawTimer, repeatChangedToZeroReleasesCallbackAtActualFinalFiring) {
  int calls = 0;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  ASSERT_TRUE(timer.start(0ms, 1ms, [&, token](auto &self) {
    ++calls;
    if (calls == 1) {
      EXPECT_TRUE(self.active()); // The next firing was already scheduled.
      EXPECT_TRUE(self.set_repeat(0ms));
    } else {
      EXPECT_EQ(calls, 2);
      EXPECT_FALSE(self.active());
    }
  }));
  token.reset();
  loop.run();
  EXPECT_EQ(calls, 2);
  EXPECT_TRUE(retained.expired());
}

TEST_F(RawTimer, againOnlyReusesActiveSubscriptionAndStopAllowsFreshStart) {
  EXPECT_EQ(timer.again().error(), uv::make_error_code(UV_EINVAL));
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  ASSERT_TRUE(timer.start(1h, 1ms, [token](auto&) { ADD_FAILURE(); }));
  token.reset();
  ASSERT_TRUE(timer.again());
  ASSERT_TRUE(timer.stop());
  EXPECT_TRUE(retained.expired());
  EXPECT_EQ(timer.again().error(), uv::make_error_code(UV_EINVAL));
  int calls = 0;
  ASSERT_TRUE(timer.start(0ms, 1ms, [&](auto &self) {
    ++calls;
    EXPECT_TRUE(self.again());
    EXPECT_TRUE(self.stop());
  }));
  loop.run();
  EXPECT_EQ(calls, 1);
}

TEST_F(RawTimer, invalidDurationsPreserveNativeScheduleRepeatAndCallback) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  ASSERT_TRUE(timer.start(1h, 2ms, [token](auto&) {}));
  token.reset();
  using floating = std::chrono::duration<double, std::milli>;
  for (auto value : {(std::numeric_limits<double>::quiet_NaN)(),
                     (std::numeric_limits<double>::infinity)(),
                     -(std::numeric_limits<double>::infinity)(), 1e100}) {
    EXPECT_EQ(timer.start(floating{value}, [](auto&) {}).error(), uv::make_error_code(UV_EINVAL));
    EXPECT_EQ(timer.start(0ms, floating{value}, [](auto&) {}).error(), uv::make_error_code(UV_EINVAL));
    EXPECT_EQ(timer.start_static<+[](uv::raw::timer&) {}>(floating{value}).error(),
              uv::make_error_code(UV_EINVAL));
    EXPECT_EQ(timer.set_repeat(floating{value}).error(), uv::make_error_code(UV_EINVAL));
  }
  using seconds = std::chrono::duration<std::uint64_t>;
  EXPECT_EQ(timer.start(seconds{(std::numeric_limits<std::uint64_t>::max)()}, [](auto&) {}).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(timer.active());
  EXPECT_EQ(timer.repeat(), 2ms);
  EXPECT_FALSE(retained.expired());
  ASSERT_TRUE(timer.stop());
  EXPECT_TRUE(retained.expired());
}

TEST_F(RawTimer, failedReentrantStartPreservesExecutingCallbackAndNativeSchedule) {
  int calls = 0;
  bool replacement_called = false;
  auto original = std::make_shared<int>(42);
  std::weak_ptr<int> retained = original;
  ASSERT_TRUE(timer.start(0ms, 1ms, [&, original, count = 0](auto &self) mutable {
    calls = ++count;
    EXPECT_EQ(*original, 42);
    EXPECT_TRUE(self.active());
    EXPECT_EQ(uv_timer_get_repeat(self.native()), std::uint64_t{1});
    if (count == 1) {
      auto replacement = std::make_shared<int>(7);
      std::weak_ptr<int> released = replacement;
      using floating = std::chrono::duration<double, std::milli>;
      auto started = self.start(0ms, floating{(std::numeric_limits<double>::infinity)()},
        [&, replacement](auto&) { replacement_called = true; });
      EXPECT_EQ(started.error(), uv::make_error_code(UV_EINVAL));
      replacement.reset();
      EXPECT_TRUE(released.expired());
      EXPECT_FALSE(retained.expired());
      EXPECT_TRUE(self.active());
      EXPECT_EQ(uv_timer_get_repeat(self.native()), std::uint64_t{1});
    } else {
      EXPECT_EQ(count, 2);
      EXPECT_TRUE(self.stop());
    }
  }));
  original.reset();
  // Bounded native loop progress: losing the callback must fail, not hang.
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (calls < 2 && std::chrono::steady_clock::now() < deadline) {
    loop.run(uv::run_mode::nowait);
  }
  EXPECT_EQ(calls, 2); // Mutable state belongs to the original callable.
  EXPECT_FALSE(replacement_called);
  EXPECT_FALSE(timer.active());
  EXPECT_TRUE(timer.stop());
  EXPECT_TRUE(retained.expired());
}

TEST_F(RawTimer, durationConversionPreservesWideNativeCountsAndTruncatesFractions) {
  const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  ASSERT_TRUE(timer.set_repeat(uv::raw::timer::duration{maximum}));
  EXPECT_EQ(timer.repeat().count(), maximum);
  ASSERT_TRUE(timer.set_repeat(std::chrono::microseconds{1999}));
  EXPECT_EQ(timer.repeat(), 1ms);
  ASSERT_TRUE(timer.set_repeat(std::chrono::duration<double, std::milli>{1.75}));
  EXPECT_EQ(timer.repeat(), 1ms);
  using fractional_seconds = std::chrono::duration<int, std::ratio<2, 3>>;
  ASSERT_TRUE(timer.set_repeat(fractional_seconds{2}));
  EXPECT_EQ(timer.repeat(), 1333ms);
  ASSERT_TRUE(timer.set_repeat(fractional_seconds{-2}));
  EXPECT_EQ(timer.repeat(), 0ms);
  ASSERT_TRUE(timer.set_repeat(-2ms));
  EXPECT_EQ(timer.repeat(), 0ms);
  int calls = 0;
  ASSERT_TRUE(timer.start(-1ms, [&](auto&) { ++calls; }));
  loop.run();
  EXPECT_EQ(calls, 1);
#if UVPP_HAS_TIMER_GET_DUE_IN
  EXPECT_EQ(timer.due_in().count(), 0u);
#endif
}

TEST_F(RawTimer, runtimeToStaticReplacementReleasesCaptureBeforeDelivery) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(timer.start(1h, [token](auto&) { ADD_FAILURE(); }));
  token.reset();
  int calls = 0;
  timer.user_data(calls);
  ASSERT_TRUE(timer.start_static<+[](uv::raw::timer &self) { ++*self.user_data<int>(); }>(0ms));
  EXPECT_TRUE(old.expired());
  loop.run();
  EXPECT_EQ(calls, 1);
}

TEST_F(RawTimer, closeReleasesSubscriptionAndRejectsAllMutations) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> retained = token;
  ASSERT_TRUE(timer.start(1h, [token](auto&) {}));
  token.reset();
  timer.close();
  EXPECT_TRUE(retained.expired());
  EXPECT_EQ(timer.start(0ms, [](auto&) {}).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(timer.start_static<+[](uv::raw::timer&) {}>(0ms).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(timer.stop().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(timer.again().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(timer.set_repeat(0ms).error(), uv::make_error_code(UV_EBADF));
}

TEST(RawLoopWatchers, timerStorageSurvivesUntilCloseCompletion) {
  uv::loop loop;
  auto timer = std::make_unique<uv::raw::timer>(loop);
  auto *native = timer->native();
  ASSERT_TRUE(timer->start(0ms, [&](auto &self) {
    EXPECT_EQ(self.native(), native);
    self.close([&](auto &closed) {
      EXPECT_EQ(closed.native(), native);
      timer.reset();
    });
  }));
  loop.run();
  EXPECT_FALSE(timer);
  loop.close();
}

} // namespace
