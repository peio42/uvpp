#include <chrono>
#include <memory>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/handles/timer.hpp"

using namespace std::chrono_literals;

namespace {

void unexpected_timer_callback(uv::raw::timer&) { ADD_FAILURE(); }

void observe_static_close(uv::raw::timer &self) {
  EXPECT_TRUE(self.initialized());
  ++*self.user_data<int>();
}

class RawTimerInit : public ::testing::Test {
protected:
  uv::loop loop;
  uv::raw::timer timer;

  ~RawTimerInit() override {
    if (timer.initialized() && !timer.closing()) timer.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }
};

TEST_F(RawTimerInit, defaultConstructionHasNoNativeRegistration) {
  static_assert(std::is_nothrow_default_constructible_v<uv::raw::timer>);
  static_assert(!std::is_move_constructible_v<uv::raw::timer>);
  static_assert(!std::is_copy_constructible_v<uv::raw::timer>);
  static_assert(noexcept(timer.init(loop)));
  static_assert(noexcept(timer.init(loop.view())));
  EXPECT_FALSE(timer.initialized());
  EXPECT_EQ(timer.native_handle()->loop, nullptr);
  EXPECT_FALSE(timer.active());
  EXPECT_FALSE(timer.closing());
  EXPECT_FALSE(timer.has_ref());
  EXPECT_EQ(timer.repeat(), 0ms);
#if UVPP_HAS_TIMER_GET_DUE_IN
  EXPECT_EQ(timer.due_in(), 0ms);
#endif
  timer.ref();
  timer.unref();
  EXPECT_FALSE(timer.has_ref());
  int handles = 0;
  loop.walk([&](uv::handle_view) { ++handles; });
  EXPECT_EQ(handles, 0);
}

TEST_F(RawTimerInit, operationsRejectUninitializedStorageAndReleaseCallable) {
  const auto badf = uv::make_error_code(UV_EBADF);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(timer.start(0ms, [token](uv::raw::timer&) { ADD_FAILURE(); }).error(), badf);
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(timer.start(0ms, 1ms, unexpected_timer_callback).error(), badf);
  EXPECT_EQ(timer.start_static<unexpected_timer_callback>(0ms).error(), badf);
  EXPECT_EQ(timer.stop().error(), badf);
  EXPECT_EQ(timer.again().error(), badf);
  EXPECT_EQ(timer.set_repeat(1ms).error(), badf);
  EXPECT_EQ(timer.fileno().error(), badf);
  EXPECT_EQ(timer.send_buffer_size().error(), badf);
  EXPECT_EQ(timer.send_buffer_size(1024).error(), badf);
  EXPECT_EQ(timer.receive_buffer_size().error(), badf);
  EXPECT_EQ(timer.receive_buffer_size(1024).error(), badf);
  EXPECT_FALSE(timer.initialized());
  ASSERT_TRUE(timer.init(loop));
  EXPECT_TRUE(timer.stop());
}

TEST_F(RawTimerInit, closeBeforeInitIsInertAndDoesNotRetainCallback) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  timer.close();
  timer.close([token](uv::raw::timer&) { ADD_FAILURE(); });
  token.reset();
  EXPECT_TRUE(weak.expired());
  timer.close_static<unexpected_timer_callback>();
  loop.run();
  EXPECT_FALSE(timer.initialized());
  EXPECT_FALSE(timer.closing());
  ASSERT_TRUE(timer.init(loop.view()));
  bool fired = false;
  ASSERT_TRUE(timer.start(0ms, [&](uv::raw::timer &self) {
    fired = true;
    self.close();
  }));
  loop.run();
  EXPECT_TRUE(fired);
}

TEST_F(RawTimerInit, initPreservesNativeAddressAndApplicationData) {
  auto *native = timer.native();
  int marker = 42;
  timer.user_data(marker);
  ASSERT_TRUE(timer.init(loop));
  EXPECT_TRUE(timer.initialized());
  EXPECT_FALSE(timer.active());
  EXPECT_TRUE(timer.has_ref());
  EXPECT_EQ(timer.native(), native);
  EXPECT_EQ(timer.native_handle()->loop, loop.native());
  EXPECT_EQ(timer.user_data<int>(), &marker);
  EXPECT_EQ(&uv::raw::timer::from_native(native), &timer);
  int handles = 0;
  loop.walk([&](uv::handle_view h) {
    ++handles;
    EXPECT_EQ(h.native_handle(), timer.native_handle());
  });
  EXPECT_EQ(handles, 1);
}

TEST_F(RawTimerInit, repeatedInitPreservesActiveSubscriptionAndLoop) {
  uv::loop other;
  ASSERT_TRUE(timer.init(loop.view()));
  bool fired = false;
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(timer.start(0ms, 10ms, [&, token](uv::raw::timer &self) {
    EXPECT_EQ(*token, 42);
    fired = true;
    self.close();
  }));
  token.reset();
  EXPECT_EQ(timer.init(loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(timer.init(other.view()).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(timer.native_handle()->loop, loop.native());
  EXPECT_TRUE(timer.active());
  EXPECT_FALSE(weak.expired());
  EXPECT_EQ(timer.repeat(), 10ms);
  EXPECT_NO_THROW(other.close());
  loop.run();
  EXPECT_TRUE(fired);
  EXPECT_TRUE(weak.expired());
}

TEST_F(RawTimerInit, initializedRemainsTrueThroughCloseCompletion) {
  ASSERT_TRUE(timer.init(loop));
  bool closed = false;
  timer.close([&](uv::raw::timer &self) {
    closed = true;
    EXPECT_TRUE(self.initialized());
    EXPECT_EQ(self.init(loop).error(), uv::make_error_code(UV_EALREADY));
  });
  EXPECT_TRUE(timer.initialized());
  EXPECT_TRUE(timer.closing());
  EXPECT_EQ(timer.init(loop.view()).error(), uv::make_error_code(UV_EALREADY));
  loop.run();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(timer.initialized());
  EXPECT_EQ(timer.native_handle()->loop, loop.native());
  EXPECT_EQ(timer.init(loop).error(), uv::make_error_code(UV_EALREADY));
}

TEST_F(RawTimerInit, throwingConstructorsShareInitializedState) {
  uv::raw::timer owned_loop{loop};
  uv::raw::timer viewed_loop{loop.view()};
  EXPECT_TRUE(owned_loop.initialized());
  EXPECT_TRUE(viewed_loop.initialized());
  EXPECT_EQ(owned_loop.init(loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(viewed_loop.init(loop.view()).error(), uv::make_error_code(UV_EALREADY));
  owned_loop.close();
  int closed = 0;
  viewed_loop.user_data(closed);
  viewed_loop.close_static<observe_static_close>();
  loop.run();
  EXPECT_EQ(closed, 1);
}

} // namespace
