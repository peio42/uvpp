#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/handles/async.hpp"

namespace {
using async = uv::raw::async;

void count_notification(async &self) { ++*self.user_data<int>(); }
void unexpected_notification(async&) { ADD_FAILURE(); }
void observe_close(async &self) {
  EXPECT_TRUE(self.initialized());
  ++*self.user_data<int>();
}
[[maybe_unused]] void invalid_notification(int) {}

template<class T>
concept accepts_invalid_init = requires(T &self, uv::loop &loop) {
  self.init(loop, typename T::template static_callback<invalid_notification>{});
};
static_assert(!accepts_invalid_init<async>);

struct throwing_copy {
  throwing_copy() = default;
  throwing_copy(const throwing_copy&) { throw std::runtime_error{"copy failed"}; }
  void operator()(async&) const {}
};

class RawAsyncInit : public ::testing::Test {
protected:
  uv::loop loop;
  async wakeup;

  ~RawAsyncInit() override {
    if (wakeup.initialized() && !wakeup.closing()) wakeup.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }

  void tick() { loop.run(uv::run_mode::nowait); }

  template<class Predicate>
  void pump_until(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!done() && std::chrono::steady_clock::now() < deadline) {
      tick();
      std::this_thread::yield();
    }
    EXPECT_TRUE(done());
  }
};

TEST_F(RawAsyncInit, defaultStorageRejectsOperationsWithoutNativeRegistration) {
  static_assert(std::is_nothrow_default_constructible_v<async>);
  static_assert(!std::is_move_constructible_v<async>);
  static_assert(!std::is_copy_constructible_v<async>);
  static_assert(noexcept(wakeup.init(loop)));
  static_assert(noexcept(wakeup.init(loop.view())));
  static_assert(noexcept(wakeup.init(loop, async::callback{})));
  static_assert(noexcept(wakeup.init(loop.view(), async::callback{})));
  static_assert(noexcept(wakeup.init(loop, async::static_callback<count_notification>{})));
  static_assert(noexcept(wakeup.init(loop.view(), async::static_callback<count_notification>{})));
  EXPECT_FALSE(wakeup.initialized());
  EXPECT_EQ(wakeup.native_handle()->loop, nullptr);
  EXPECT_FALSE(wakeup.active());
  EXPECT_FALSE(wakeup.closing());
  wakeup.ref();
  wakeup.unref();
  EXPECT_FALSE(wakeup.has_ref());
  const auto badf = uv::make_error_code(UV_EBADF);
  EXPECT_EQ(wakeup.send().error(), badf);
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(wakeup.set_callback([token](async&) {}).error(), badf);
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(wakeup.set_callback_static<unexpected_notification>().error(), badf);
  EXPECT_EQ(wakeup.fileno().error(), badf);
  EXPECT_EQ(wakeup.send_buffer_size().error(), badf);
  EXPECT_EQ(wakeup.send_buffer_size(1024).error(), badf);
  EXPECT_EQ(wakeup.receive_buffer_size().error(), badf);
  EXPECT_EQ(wakeup.receive_buffer_size(1024).error(), badf);
  EXPECT_TRUE(loop.handles().empty());
  EXPECT_FALSE(loop.alive());
}

TEST_F(RawAsyncInit, closeBeforeInitIsInertAndInitPreservesStorageAndData) {
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  wakeup.close();
  wakeup.close([token](async&) { ADD_FAILURE(); });
  token.reset();
  EXPECT_TRUE(weak.expired());
  wakeup.close_static<observe_close>();
  loop.run();
  EXPECT_FALSE(wakeup.initialized());
  auto *native = wakeup.native();
  int marker = 42;
  wakeup.user_data(marker);
  ASSERT_TRUE(wakeup.init(loop.view()));
  EXPECT_TRUE(wakeup.initialized());
  EXPECT_TRUE(wakeup.active());
  EXPECT_TRUE(wakeup.has_ref());
  EXPECT_TRUE(loop.alive());
  EXPECT_EQ(wakeup.native(), native);
  EXPECT_EQ(wakeup.user_data<int>(), &marker);
  EXPECT_EQ(&async::from_native(native), &wakeup);
  EXPECT_EQ(wakeup.native_handle()->loop, loop.native());
  // Empty init drops notifications, while keeping the handle active/referenced.
  ASSERT_TRUE(wakeup.send());
  tick();
  EXPECT_EQ(marker, 42);
  EXPECT_TRUE(wakeup.active());
  EXPECT_TRUE(wakeup.has_ref());
  ASSERT_TRUE(wakeup.set_callback_static<count_notification>());
  ASSERT_TRUE(wakeup.send());
  pump_until([&] { return marker == 43; });
}

TEST_F(RawAsyncInit, allLoopViewAndCallbackInitOverloadsDeliver) {
  std::array<async, 6> handles{};
  std::array<int, 6> counts{};
  ASSERT_TRUE(handles[0].init(loop));
  ASSERT_TRUE(handles[1].init(loop.view()));
  ASSERT_TRUE(handles[2].init(loop, count_notification));
  ASSERT_TRUE(handles[3].init(loop.view(), count_notification));
  ASSERT_TRUE(handles[4].init(loop, async::static_callback<count_notification>{}));
  ASSERT_TRUE(handles[5].init(loop.view(), async::static_callback<count_notification>{}));
  for (std::size_t i = 0; i < handles.size(); ++i) {
    handles[i].user_data(counts[i]);
    EXPECT_TRUE(handles[i].active());
    EXPECT_TRUE(handles[i].initialized());
    EXPECT_TRUE(handles[i].send());
  }
  pump_until([&] { return counts[2] && counts[3] && counts[4] && counts[5]; });
  EXPECT_EQ(counts[0], 0);
  EXPECT_EQ(counts[1], 0);
  for (std::size_t i = 2; i < handles.size(); ++i) EXPECT_EQ(counts[i], 1);
  for (auto &handle : handles) handle.close();
  loop.run();
}

TEST_F(RawAsyncInit, repeatedInitPreservesPendingRuntimeCallbackAndLoop) {
  uv::loop other;
  int calls = 0;
  auto original = std::make_shared<int>(42);
  std::weak_ptr<int> retained = original;
  ASSERT_TRUE(wakeup.init(loop, [&, original, count = 0](async&) mutable {
    EXPECT_EQ(*original, 42);
    calls = ++count;
  }));
  original.reset();
  ASSERT_TRUE(wakeup.send());
  const auto native_callback = wakeup.native()->async_cb;
  auto rejected = std::make_shared<int>(7);
  std::weak_ptr<int> released = rejected;
  EXPECT_EQ(wakeup.init(other, [rejected](async&) { ADD_FAILURE(); }).error(),
            uv::make_error_code(UV_EALREADY));
  rejected.reset();
  EXPECT_TRUE(released.expired());
  EXPECT_EQ(wakeup.init(other.view(), async::static_callback<unexpected_notification>{}).error(),
            uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(wakeup.init(loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(wakeup.native_handle()->loop, loop.native());
  EXPECT_EQ(wakeup.native()->async_cb, native_callback);
  EXPECT_FALSE(retained.expired());
  EXPECT_NO_THROW(other.close());
  pump_until([&] { return calls == 1; });
  ASSERT_TRUE(wakeup.send());
  pump_until([&] { return calls == 2; });
}

TEST_F(RawAsyncInit, repeatedInitPreservesPendingStaticSelection) {
  int calls = 0;
  wakeup.user_data(calls);
  ASSERT_TRUE(wakeup.init(loop, async::static_callback<count_notification>{}));
  ASSERT_TRUE(wakeup.send());
  EXPECT_EQ(wakeup.init(loop.view(), unexpected_notification).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(wakeup.init(loop.view(), async::static_callback<unexpected_notification>{}).error(),
            uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(wakeup.init(loop.view()).error(), uv::make_error_code(UV_EALREADY));
  pump_until([&] { return calls == 1; });
}

TEST_F(RawAsyncInit, reentrantRejectedInitRestoresExecutingRuntimeCallable) {
  int calls = 0;
  ASSERT_TRUE(wakeup.init(loop.view(), [&, count = 0](async &self) mutable {
    calls = ++count;
    if (count == 1) {
      EXPECT_EQ(self.init(loop, unexpected_notification).error(), uv::make_error_code(UV_EALREADY));
      EXPECT_EQ(self.init(loop, async::static_callback<unexpected_notification>{}).error(),
                uv::make_error_code(UV_EALREADY));
      EXPECT_TRUE(self.send());
    } else self.close();
  }));
  ASSERT_TRUE(wakeup.send());
  pump_until([&] { return calls == 2; });
}

TEST_F(RawAsyncInit, closeRetainsSentinelAndRejectsAllInitForms) {
  ASSERT_TRUE(wakeup.init(loop));
  bool closed = false;
  wakeup.close([&](async &self) {
    closed = true;
    EXPECT_TRUE(self.initialized());
    EXPECT_EQ(self.init(loop).error(), uv::make_error_code(UV_EALREADY));
  });
  EXPECT_TRUE(wakeup.initialized());
  EXPECT_TRUE(wakeup.closing());
  const auto already = uv::make_error_code(UV_EALREADY);
  EXPECT_EQ(wakeup.init(loop).error(), already);
  EXPECT_EQ(wakeup.init(loop.view()).error(), already);
  EXPECT_EQ(wakeup.init(loop, unexpected_notification).error(), already);
  EXPECT_EQ(wakeup.init(loop.view(), unexpected_notification).error(), already);
  EXPECT_EQ(wakeup.init(loop, async::static_callback<unexpected_notification>{}).error(), already);
  EXPECT_EQ(wakeup.init(loop.view(), async::static_callback<unexpected_notification>{}).error(), already);
  loop.run();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(wakeup.initialized());
  EXPECT_EQ(wakeup.native_handle()->loop, loop.native());
  EXPECT_EQ(wakeup.init(loop).error(), already);
  EXPECT_EQ(wakeup.set_callback(unexpected_notification).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(wakeup.set_callback_static<unexpected_notification>().error(), uv::make_error_code(UV_EBADF));
  // send during/after close remains outside the raw protocol and is not tested.
}

TEST_F(RawAsyncInit, callablePreparationFailureLeavesUninitializedStorageRetryable) {
  throwing_copy callback;
  EXPECT_THROW(wakeup.init(loop, callback), std::runtime_error);
  EXPECT_THROW(wakeup.init(loop.view(), callback), std::runtime_error);
  EXPECT_FALSE(wakeup.initialized());
  EXPECT_TRUE(loop.handles().empty());
  int calls = 0;
  wakeup.user_data(calls);
  ASSERT_TRUE(wakeup.init(loop, async::static_callback<count_notification>{}));
  ASSERT_TRUE(wakeup.send());
  pump_until([&] { return calls == 1; });
}

TEST_F(RawAsyncInit, preparationFailurePreservesInitializedPendingCallback) {
  int calls = 0;
  wakeup.user_data(calls);
  ASSERT_TRUE(wakeup.init(loop.view(), async::static_callback<count_notification>{}));
  ASSERT_TRUE(wakeup.send());
  throwing_copy callback;
  EXPECT_THROW(wakeup.init(loop, callback), std::runtime_error);
  EXPECT_THROW(wakeup.init(loop.view(), callback), std::runtime_error);
  pump_until([&] { return calls == 1; });
}

TEST_F(RawAsyncInit, successfulInitCanBePublishedToProducerBeforeSend) {
  const auto loop_thread = std::this_thread::get_id();
  int calls = 0;
  ASSERT_TRUE(wakeup.init(loop, [&](async &self) {
    ++calls;
    EXPECT_EQ(std::this_thread::get_id(), loop_thread);
    self.close();
  }));
  uv::status submitted;
  std::thread producer{[&] { submitted = wakeup.send(); }};
  producer.join(); // Join before the callback can request close.
  EXPECT_TRUE(submitted);
  pump_until([&] { return calls == 1; });
}

TEST_F(RawAsyncInit, staticCloseAfterExplicitInitRetainsNativeLifetime) {
  ASSERT_TRUE(wakeup.init(loop.view()));
  int closed = 0;
  wakeup.user_data(closed);
  wakeup.close_static<observe_close>();
  loop.run();
  EXPECT_EQ(closed, 1);
  EXPECT_TRUE(wakeup.initialized());
}

} // namespace
