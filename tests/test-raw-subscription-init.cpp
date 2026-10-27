#include <chrono>
#include <csignal>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/handles/fs_event.hpp"
#include "uvpp/handles/signal.hpp"

namespace {

struct watched_directory {
  std::filesystem::path path;
  watched_directory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned int attempt = 0;; ++attempt) {
      path = std::filesystem::temp_directory_path() /
        ("uvpp-subscription-init-" + std::to_string(stamp) + "-" + std::to_string(attempt));
      if (std::filesystem::create_directory(path)) break;
    }
  }
  ~watched_directory() { std::filesystem::remove_all(path); }
};

void unexpected_signal(uv::raw::signal&, uv::signal_number) { ADD_FAILURE(); }
void unexpected_fs_event(uv::raw::fs_event&, uv::raw::fs_event_result) { ADD_FAILURE(); }

template<class Watcher>
void observe_static_close(Watcher &self) {
  EXPECT_TRUE(self.initialized());
  ++*self.template user_data<int>();
}

template<class Watcher>
uv::status start_subscription(Watcher &self, const std::string &path, typename Watcher::callback cb) {
  if constexpr (std::is_same_v<Watcher, uv::raw::signal>) return self.start(SIGINT, std::move(cb));
  else return self.start(path, std::move(cb));
}

template<class Watcher>
uv::status start_static_subscription(Watcher &self, const std::string &path) {
  if constexpr (std::is_same_v<Watcher, uv::raw::signal>) return self.template start_static<unexpected_signal>(SIGINT);
  else return self.template start_static<unexpected_fs_event>(path);
}

template<class Watcher>
void dispatch_notification(Watcher &self) {
  // Exercise the registered trampoline without OS signal or event timing assumptions.
  if constexpr (std::is_same_v<Watcher, uv::raw::signal>) self.native()->signal_cb(self.native(), SIGINT);
  else self.native()->cb(self.native(), "marker", UV_CHANGE, 0);
}

template<class Watcher>
class RawSubscriptionInit : public ::testing::Test {
protected:
  watched_directory dir;
  uv::loop loop;
  Watcher watcher;

  ~RawSubscriptionInit() override {
    if (watcher.initialized() && !watcher.closing()) watcher.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }
};

using subscription_watchers = ::testing::Types<uv::raw::signal, uv::raw::fs_event>;
TYPED_TEST_SUITE(RawSubscriptionInit, subscription_watchers);

TYPED_TEST(RawSubscriptionInit, defaultConstructionHasNoNativeRegistration) {
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

TYPED_TEST(RawSubscriptionInit, operationsRejectUninitializedStorageAndReleaseCallable) {
  const auto badf = uv::make_error_code(UV_EBADF);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(start_subscription(this->watcher, this->dir.path.string(),
    [token](TypeParam&, auto) { ADD_FAILURE(); }).error(), badf);
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_EQ(start_static_subscription(this->watcher, this->dir.path.string()).error(), badf);
  EXPECT_EQ(this->watcher.stop().error(), badf);
  EXPECT_EQ(this->watcher.fileno().error(), badf);
  EXPECT_EQ(this->watcher.send_buffer_size().error(), badf);
  EXPECT_EQ(this->watcher.send_buffer_size(1024).error(), badf);
  EXPECT_EQ(this->watcher.receive_buffer_size().error(), badf);
  EXPECT_EQ(this->watcher.receive_buffer_size(1024).error(), badf);
  if constexpr (std::is_same_v<TypeParam, uv::raw::signal>) {
    EXPECT_EQ(this->watcher.start_oneshot(SIGINT, unexpected_signal).error(), badf);
    EXPECT_EQ(this->watcher.template start_oneshot_static<unexpected_signal>(SIGINT).error(), badf);
  } else {
    EXPECT_EQ(this->watcher.path().error(), badf);
    const std::string_view invalid{"bad\0path", 8};
    EXPECT_EQ(this->watcher.start(invalid, 0, unexpected_fs_event).error(), badf);
    EXPECT_EQ(this->watcher.start(invalid, uv::raw::fs_event_flag::recursive, unexpected_fs_event).error(), badf);
    EXPECT_EQ(this->watcher.template start_static<unexpected_fs_event>(invalid).error(), badf);
    EXPECT_EQ(this->watcher.template start_static<unexpected_fs_event>(invalid, uv::raw::fs_event_flag::recursive).error(), badf);
  }
  EXPECT_FALSE(this->watcher.initialized());
  ASSERT_TRUE(this->watcher.init(this->loop));
  EXPECT_TRUE(this->watcher.stop());
}

TYPED_TEST(RawSubscriptionInit, closeBeforeInitIsInertAndDoesNotRetainCallback) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  this->watcher.close();
  this->watcher.close([token](TypeParam&) { ADD_FAILURE(); });
  token.reset();
  EXPECT_TRUE(weak.expired());
  this->watcher.template close_static<observe_static_close<TypeParam>>();
  this->loop.run();
  EXPECT_FALSE(this->watcher.initialized());
  EXPECT_FALSE(this->watcher.closing());
  ASSERT_TRUE(this->watcher.init(this->loop.view()));
  int called = 0;
  ASSERT_TRUE(start_subscription(this->watcher, this->dir.path.string(),
    [&](TypeParam &self, auto) { ++called; EXPECT_TRUE(self.stop()); }));
  dispatch_notification(this->watcher);
  EXPECT_EQ(called, 1);
  EXPECT_FALSE(this->watcher.active());
}

TYPED_TEST(RawSubscriptionInit, initPreservesNativeAddressAndApplicationData) {
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

TYPED_TEST(RawSubscriptionInit, repeatedInitPreservesActiveSubscriptionAndLoop) {
  uv::loop other;
  ASSERT_TRUE(this->watcher.init(this->loop.view()));
  int called = 0;
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(start_subscription(this->watcher, this->dir.path.string(),
    [&, token](TypeParam &self, auto) { ++called; EXPECT_EQ(*token, 42); EXPECT_TRUE(self.stop()); }));
  token.reset();
  EXPECT_EQ(this->watcher.init(this->loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(this->watcher.init(other.view()).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(this->watcher.native_handle()->loop, this->loop.native());
  EXPECT_TRUE(this->watcher.active());
  EXPECT_FALSE(weak.expired());
  if constexpr (std::is_same_v<TypeParam, uv::raw::fs_event>) {
    EXPECT_EQ(this->watcher.path().value(), this->dir.path.string());
  }
  EXPECT_NO_THROW(other.close());
  dispatch_notification(this->watcher);
  EXPECT_EQ(called, 1);
  EXPECT_TRUE(weak.expired());
}

TYPED_TEST(RawSubscriptionInit, initializedRemainsTrueThroughCloseCompletion) {
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
  if constexpr (std::is_same_v<TypeParam, uv::raw::fs_event>) {
    EXPECT_EQ(this->watcher.path().error(), uv::make_error_code(UV_EBADF));
  }
  this->loop.run();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(this->watcher.initialized());
  EXPECT_EQ(this->watcher.init(this->loop).error(), uv::make_error_code(UV_EALREADY));
}

TYPED_TEST(RawSubscriptionInit, throwingConstructorsShareInitializedState) {
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

TYPED_TEST(RawSubscriptionInit, rejectedStartKeepsInitializationAndAllowsFreshSubscription) {
  ASSERT_TRUE(this->watcher.init(this->loop));
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  if constexpr (std::is_same_v<TypeParam, uv::raw::signal>) {
    EXPECT_EQ(this->watcher.start_oneshot(0, [token](auto&, auto) {}).error(),
              uv::make_error_code(UV_EINVAL));
  } else {
    EXPECT_FALSE(this->watcher.start((this->dir.path / "missing").string(),
                                    [token](auto&, auto) {}));
    EXPECT_FALSE(this->watcher.path());
  }
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_TRUE(this->watcher.initialized());
  EXPECT_FALSE(this->watcher.active());
  int calls = 0;
  ASSERT_TRUE(start_subscription(this->watcher, this->dir.path.string(),
    [&](TypeParam &self, auto) { ++calls; EXPECT_TRUE(self.stop()); }));
  dispatch_notification(this->watcher);
  EXPECT_EQ(calls, 1);
}

TEST(RawSignalInit, duplicateInitDuringOneShotDeliveryPreservesRearming) {
  uv::loop loop;
  uv::raw::signal watcher;
  ASSERT_TRUE(watcher.init(loop));
  int called = 0;
  ASSERT_TRUE(watcher.start_oneshot(SIGINT, [&](uv::raw::signal &self, int) {
    ++called;
    EXPECT_FALSE(self.active());
    EXPECT_EQ(self.init(loop).error(), uv::make_error_code(UV_EALREADY));
    EXPECT_TRUE(self.start_oneshot(SIGINT, [&](uv::raw::signal &last, int) {
      ++called;
      EXPECT_FALSE(last.active());
    }));
  }));
  dispatch_notification(watcher);
  EXPECT_TRUE(watcher.active());
  dispatch_notification(watcher);
  EXPECT_EQ(called, 2);
  EXPECT_FALSE(watcher.active());
  watcher.close();
  loop.run();
  loop.close();
}

} // namespace
