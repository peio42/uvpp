#include <memory>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/handles/check.hpp"
#include "uvpp/handles/idle.hpp"
#include "uvpp/handles/prepare.hpp"

namespace {

template<class Watcher>
void unexpected_phase_callback(Watcher&) { ADD_FAILURE(); }

template<class Watcher>
void observe_static_close(Watcher &self) {
  EXPECT_TRUE(self.initialized());
  ++*self.template user_data<int>();
}

template<class Watcher>
class RawPhaseInit : public ::testing::Test {
protected:
  uv::loop loop;
  Watcher watcher;

  ~RawPhaseInit() override {
    if (this->watcher.initialized() && !this->watcher.closing()) this->watcher.close();
    this->loop.run();
    EXPECT_NO_THROW(this->loop.close());
  }
};

using explicit_phase_watchers = ::testing::Types<uv::raw::idle, uv::raw::prepare, uv::raw::check>;
TYPED_TEST_SUITE(RawPhaseInit, explicit_phase_watchers);

TYPED_TEST(RawPhaseInit, defaultConstructionHasNoNativeRegistration) {
  static_assert(std::is_nothrow_default_constructible_v<TypeParam>);
  static_assert(!std::is_move_constructible_v<TypeParam>);
  static_assert(!std::is_copy_constructible_v<TypeParam>);
  static_assert(noexcept(this->watcher.init(this->loop)));
  static_assert(noexcept(this->watcher.init(this->loop.view())));
  EXPECT_FALSE(this->watcher.initialized());
  EXPECT_EQ(this->watcher.native_handle()->loop, nullptr);
  EXPECT_FALSE(this->watcher.active());
  EXPECT_FALSE(this->watcher.closing());
  EXPECT_FALSE(this->watcher.has_ref());
  this->watcher.ref();
  this->watcher.unref();
  EXPECT_FALSE(this->watcher.has_ref());
  int handles = 0;
  this->loop.walk([&](uv::handle_view) { ++handles; });
  EXPECT_EQ(handles, 0);
}

TYPED_TEST(RawPhaseInit, operationsRejectUninitializedStorageAndReleaseCallable) {
  const auto badf = uv::make_error_code(UV_EBADF);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(this->watcher.start([token](TypeParam&) { ADD_FAILURE(); }).error(), badf);
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(this->watcher.template start_static<unexpected_phase_callback<TypeParam>>().error(), badf);
  EXPECT_EQ(this->watcher.stop().error(), badf);
  EXPECT_EQ(this->watcher.fileno().error(), badf);
  EXPECT_EQ(this->watcher.send_buffer_size().error(), badf);
  EXPECT_EQ(this->watcher.send_buffer_size(1024).error(), badf);
  EXPECT_EQ(this->watcher.receive_buffer_size().error(), badf);
  EXPECT_EQ(this->watcher.receive_buffer_size(1024).error(), badf);
  EXPECT_FALSE(this->watcher.initialized());
  ASSERT_TRUE(this->watcher.init(this->loop));
  EXPECT_TRUE(this->watcher.stop());
}

TYPED_TEST(RawPhaseInit, closeBeforeInitIsInertAndDoesNotRetainCallback) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  this->watcher.close();
  this->watcher.close([token](TypeParam&) { ADD_FAILURE(); });
  token.reset();
  EXPECT_TRUE(weak.expired());
  this->watcher.template close_static<unexpected_phase_callback<TypeParam>>();
  this->loop.run();
  EXPECT_FALSE(this->watcher.initialized());
  EXPECT_FALSE(this->watcher.closing());
  ASSERT_TRUE(this->watcher.init(this->loop.view()));
  bool fired = false;
  ASSERT_TRUE(this->watcher.start([&](TypeParam &self) {
    fired = true;
    self.close();
  }));
  this->loop.run(uv::run_mode::nowait);
  if (!this->watcher.closing()) this->watcher.close();
  this->loop.run();
  EXPECT_TRUE(fired);
}

TYPED_TEST(RawPhaseInit, initPreservesNativeAddressAndApplicationData) {
  auto *native = this->watcher.native();
  int marker = 42;
  this->watcher.user_data(marker);
  ASSERT_TRUE(this->watcher.init(this->loop));
  EXPECT_TRUE(this->watcher.initialized());
  EXPECT_FALSE(this->watcher.active());
  EXPECT_TRUE(this->watcher.has_ref());
  EXPECT_EQ(this->watcher.native(), native);
  EXPECT_EQ(this->watcher.native_handle()->loop, this->loop.native());
  EXPECT_EQ(this->watcher.template user_data<int>(), &marker);
  EXPECT_EQ(&TypeParam::from_native(native), &this->watcher);
  int handles = 0;
  this->loop.walk([&](uv::handle_view h) {
    ++handles;
    EXPECT_EQ(h.native_handle(), this->watcher.native_handle());
  });
  EXPECT_EQ(handles, 1);
}

TYPED_TEST(RawPhaseInit, repeatedInitPreservesActiveSubscriptionAndLoop) {
  uv::loop other;
  ASSERT_TRUE(this->watcher.init(this->loop.view()));
  bool fired = false;
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(this->watcher.start([&, token](TypeParam &self) {
    EXPECT_EQ(*token, 42);
    fired = true;
    self.close();
  }));
  token.reset();
  EXPECT_EQ(this->watcher.init(this->loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(this->watcher.init(other.view()).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(this->watcher.native_handle()->loop, this->loop.native());
  EXPECT_TRUE(this->watcher.active());
  EXPECT_FALSE(weak.expired());
  EXPECT_NO_THROW(other.close());
  this->loop.run(uv::run_mode::nowait);
  if (!this->watcher.closing()) this->watcher.close();
  this->loop.run();
  EXPECT_TRUE(fired);
  EXPECT_TRUE(weak.expired());
}

TYPED_TEST(RawPhaseInit, initializedRemainsTrueThroughCloseCompletion) {
  ASSERT_TRUE(this->watcher.init(this->loop));
  bool closed = false;
  this->watcher.close([&](TypeParam &self) {
    closed = true;
    EXPECT_TRUE(self.initialized());
    EXPECT_EQ(self.init(this->loop).error(), uv::make_error_code(UV_EALREADY));
  });
  EXPECT_TRUE(this->watcher.initialized());
  EXPECT_TRUE(this->watcher.closing());
  EXPECT_EQ(this->watcher.init(this->loop.view()).error(), uv::make_error_code(UV_EALREADY));
  this->loop.run();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(this->watcher.initialized());
  EXPECT_EQ(this->watcher.native_handle()->loop, this->loop.native());
  EXPECT_EQ(this->watcher.init(this->loop).error(), uv::make_error_code(UV_EALREADY));
}

TYPED_TEST(RawPhaseInit, throwingConstructorsShareInitializedState) {
  TypeParam owned_loop{this->loop};
  TypeParam viewed_loop{this->loop.view()};
  EXPECT_TRUE(owned_loop.initialized());
  EXPECT_TRUE(viewed_loop.initialized());
  EXPECT_EQ(owned_loop.init(this->loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(viewed_loop.init(this->loop.view()).error(), uv::make_error_code(UV_EALREADY));
  owned_loop.close();
  int closed = 0;
  viewed_loop.user_data(closed);
  viewed_loop.template close_static<observe_static_close<TypeParam>>();
  this->loop.run();
  EXPECT_EQ(closed, 1);
}

} // namespace
