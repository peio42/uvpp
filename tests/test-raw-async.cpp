#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "gtest/gtest.h"
#include "uvpp/handles/async.hpp"
#include "uvpp/handles/timer.hpp"

using namespace std::chrono_literals;

namespace {

using async = uv::raw::async;

void count_notification(async &self) {
  ++*self.user_data<int>();
}

void unexpected_notification(async&) {
  ADD_FAILURE() << "Replaced or cleared callback was delivered";
}

[[maybe_unused]] void invalid_callback(int) {}
template<class T>
concept accepts_invalid_static_callback = requires(T &value) {
  value.template set_callback_static<invalid_callback>();
};
static_assert(!accepts_invalid_static_callback<async>);
static_assert(!std::is_copy_constructible_v<async>);
static_assert(!std::is_move_constructible_v<async>);
static_assert(!std::is_convertible_v<async&, uv_async_t*>);
static_assert(!std::is_convertible_v<async&, uv::handle_view>);
static_assert(std::is_standard_layout_v<async::native_storage>);
static_assert(std::is_same_v<decltype(std::declval<async&>().send()), uv::status>);
static_assert(noexcept(std::declval<async&>().send()));

class RawAsync : public ::testing::Test {
protected:
  uv::loop loop;
  async wakeup{loop.view()};

  ~RawAsync() override {
    if (!wakeup.closing()) wakeup.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }

  void tick() { loop.run(uv::run_mode::nowait); }

  template<class Predicate>
  void pump_until(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done() && std::chrono::steady_clock::now() < deadline) {
      tick();
      std::this_thread::yield();
    }
    EXPECT_TRUE(done());
  }
};

TEST_F(RawAsync, initializesActiveStableStorageWithApplicationData) {
  int marker = 42;
  wakeup.user_data(marker);
  auto *native = wakeup.native();
  EXPECT_TRUE(wakeup.active());
  EXPECT_TRUE(wakeup.has_ref());
  EXPECT_TRUE(loop.alive());
  EXPECT_EQ(&wakeup, &async::from_native(native));
  EXPECT_EQ(&wakeup, &async::from_native(wakeup.native_handle()));
  // Empty construction installs no callable, but still accepts notifications.
  ASSERT_TRUE(wakeup.send());
  tick();
  EXPECT_EQ(wakeup.native(), native);
  EXPECT_EQ(wakeup.user_data<int>(), &marker);
}

TEST(RawAsyncConstruction, runtimeAndStaticConstructorsShareLoopAndLoopView) {
  uv::loop loop;
  async runtime_owner{loop, count_notification};
  async runtime_view{loop.view(), count_notification};
  async static_owner{loop, async::static_callback<count_notification>{}};
  async static_view{loop.view(), async::static_callback<count_notification>{}};
  std::array<async*, 4> handles{&runtime_owner, &runtime_view, &static_owner, &static_view};
  std::array<int, 4> counts{};
  for (std::size_t index = 0; index < handles.size(); ++index) {
    handles[index]->user_data(counts[index]);
    EXPECT_TRUE(handles[index]->send());
  }
  const auto all_delivered = [&] {
    for (auto count : counts) if (count == 0) return false;
    return true;
  };
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!all_delivered() && std::chrono::steady_clock::now() < deadline) {
    loop.run(uv::run_mode::nowait);
  }
  for (auto count : counts) EXPECT_EQ(count, 1);
  for (auto *handle : handles) handle->close();
  loop.run();
  loop.close();
}

TEST_F(RawAsync, coalescesBurstAndPreservesMutableRuntimeCallback) {
  int observed = 0;
  ASSERT_TRUE(wakeup.set_callback([&, count = 0](async&) mutable { observed = ++count; }));
  for (int index = 0; index < 100; ++index) EXPECT_TRUE(wakeup.send());
  tick();
  EXPECT_EQ(observed, 1);
  ASSERT_TRUE(wakeup.send());
  tick();
  EXPECT_EQ(observed, 2);
  EXPECT_TRUE(wakeup.active());
}

TEST_F(RawAsync, runtimeReplacementAppliesToPendingNotification) {
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(wakeup.set_callback([token](async&) { ADD_FAILURE(); }));
  token.reset();
  ASSERT_TRUE(wakeup.send());
  int calls = 0;
  ASSERT_TRUE(wakeup.set_callback([&](async&) { ++calls; }));
  EXPECT_TRUE(old.expired());
  tick();
  EXPECT_EQ(calls, 1);
}

TEST_F(RawAsync, runtimeToStaticReplacementReleasesCaptureAndUsesPendingNotification) {
  int calls = 0;
  wakeup.user_data(calls);
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(wakeup.set_callback([token](async&) { ADD_FAILURE(); }));
  token.reset();
  ASSERT_TRUE(wakeup.send());
  ASSERT_TRUE(wakeup.set_callback_static<count_notification>());
  EXPECT_TRUE(old.expired());
  tick();
  EXPECT_EQ(calls, 1);
}

TEST_F(RawAsync, staticToRuntimeAndStaticToStaticReplacementUseCurrentTarget) {
  int calls = 0;
  wakeup.user_data(calls);
  ASSERT_TRUE(wakeup.set_callback_static<unexpected_notification>());
  ASSERT_TRUE(wakeup.send());
  ASSERT_TRUE(wakeup.set_callback([&](async&) { ++calls; }));
  tick();
  EXPECT_EQ(calls, 1);
  ASSERT_TRUE(wakeup.set_callback_static<unexpected_notification>());
  ASSERT_TRUE(wakeup.send());
  ASSERT_TRUE(wakeup.set_callback_static<count_notification>());
  tick();
  EXPECT_EQ(calls, 2);
}

TEST_F(RawAsync, reentrantRuntimeReplacementPreservesExecutingCaptureOnlyUntilReturn) {
  int first = 0;
  int next = 0;
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(wakeup.set_callback([&, token](async &self) {
    ++first;
    EXPECT_TRUE(self.set_callback([&](async &last) {
      ++next;
      EXPECT_TRUE(old.expired());
      last.close();
    }));
    EXPECT_EQ(*token, 42);
    EXPECT_FALSE(old.expired());
    EXPECT_TRUE(self.send());
  }));
  token.reset();
  ASSERT_TRUE(wakeup.send());
  pump_until([&] { return wakeup.closing(); });
  EXPECT_EQ(first, 1);
  EXPECT_EQ(next, 1);
  EXPECT_TRUE(old.expired());
}

struct rearm_context {
  int calls = 0;
  std::weak_ptr<int> original;
};

void rearm_runtime(async &self) {
  auto &state = *self.user_data<rearm_context>();
  ++state.calls;
  EXPECT_TRUE(state.original.expired());
  EXPECT_TRUE(self.set_callback([](async &last) {
    ++last.user_data<rearm_context>()->calls;
    last.close();
  }));
  EXPECT_TRUE(self.send());
}

TEST_F(RawAsync, reentrantRuntimeToStaticToRuntimeDoesNotRestoreOldSlot) {
  rearm_context state;
  wakeup.user_data(state);
  auto token = std::make_shared<int>(42);
  state.original = token;
  ASSERT_TRUE(wakeup.set_callback([&, token](async &self) {
    ++state.calls;
    EXPECT_TRUE(self.set_callback_static<rearm_runtime>());
    EXPECT_EQ(*token, 42);
    EXPECT_FALSE(state.original.expired());
    EXPECT_TRUE(self.send());
  }));
  token.reset();
  ASSERT_TRUE(wakeup.send());
  pump_until([&] { return wakeup.closing(); });
  EXPECT_EQ(state.calls, 3);
  EXPECT_TRUE(state.original.expired());
}

void count_and_close(async &self) {
  ++*self.user_data<int>();
  self.close();
}

void replace_static_during_delivery(async &self) {
  ++*self.user_data<int>();
  EXPECT_TRUE(self.set_callback_static<count_and_close>());
  EXPECT_TRUE(self.send());
}

TEST_F(RawAsync, reentrantStaticReplacementUsesNewTargetOnNextNotification) {
  int calls = 0;
  wakeup.user_data(calls);
  ASSERT_TRUE(wakeup.set_callback_static<replace_static_during_delivery>());
  ASSERT_TRUE(wakeup.send());
  pump_until([&] { return wakeup.closing(); });
  EXPECT_EQ(calls, 2);
}

TEST_F(RawAsync, clearingCallbackReleasesCapturesWithoutStoppingNativeSource) {
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(wakeup.set_callback([token](async&) { ADD_FAILURE(); }));
  token.reset();
  ASSERT_TRUE(wakeup.set_callback({}));
  EXPECT_TRUE(old.expired());
  EXPECT_TRUE(wakeup.active());
  EXPECT_TRUE(loop.alive());
  ASSERT_TRUE(wakeup.send());
  tick();
  ASSERT_TRUE(wakeup.set_callback_static<unexpected_notification>());
  ASSERT_TRUE(wakeup.set_callback({}));
  ASSERT_TRUE(wakeup.send());
  tick();
  int calls = 0;
  wakeup.user_data(calls);
  ASSERT_TRUE(wakeup.set_callback_static<count_notification>());
  ASSERT_TRUE(wakeup.send());
  tick();
  EXPECT_EQ(calls, 1);
}

TEST_F(RawAsync, closeReleasesCapturesDiscardsPendingDeliveryAndRejectsSetters) {
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> old = token;
  ASSERT_TRUE(wakeup.set_callback([token](async&) { ADD_FAILURE(); }));
  token.reset();
  ASSERT_TRUE(wakeup.send());
  bool closed = false;
  wakeup.close([&](async&) { closed = true; });
  EXPECT_TRUE(old.expired());
  EXPECT_FALSE(closed);
  auto rejected = std::make_shared<int>(7);
  std::weak_ptr<int> released = rejected;
  EXPECT_EQ(wakeup.set_callback([rejected](async&) {}).error(), uv::make_error_code(UV_EBADF));
  rejected.reset();
  EXPECT_TRUE(released.expired());
  EXPECT_EQ(wakeup.set_callback_static<count_notification>().error(), uv::make_error_code(UV_EBADF));
  loop.run(); // Join close completion without assuming one backend poll is enough.
  EXPECT_TRUE(closed);
}

struct owner_context {
  std::unique_ptr<async> *owner;
  bool closed = false;
};

void release_closed_owner(async &self) {
  auto &state = *self.user_data<owner_context>();
  state.closed = true;
  state.owner->reset();
}

TEST(RawAsyncLifetime, staticCloseCallbackCanReleaseNativeStorage) {
  uv::loop loop;
  auto wakeup = std::make_unique<async>(loop);
  owner_context state{&wakeup};
  wakeup->user_data(state);
  EXPECT_TRUE(wakeup->set_callback([](async &self) {
    self.close_static<release_closed_owner>();
  }));
  EXPECT_TRUE(wakeup->send());
  loop.run();
  EXPECT_TRUE(state.closed);
  EXPECT_FALSE(wakeup);
  loop.close();
}

TEST_F(RawAsync, unrefAffectsLoopLivenessWithoutStoppingHandle) {
  int calls = 0;
  wakeup.user_data(calls);
  ASSERT_TRUE(wakeup.set_callback_static<count_notification>());
  wakeup.unref();
  EXPECT_FALSE(wakeup.has_ref());
  EXPECT_FALSE(loop.alive());
  EXPECT_TRUE(wakeup.active());
  ASSERT_TRUE(wakeup.send());
  wakeup.ref();
  EXPECT_TRUE(loop.alive());
  tick();
  EXPECT_EQ(calls, 1);
}

TEST_F(RawAsync, workerNotificationRunsOnLoopAndJoinsProducerBeforeClose) {
  const auto loop_thread = std::this_thread::get_id();
  std::jthread producer;
  std::atomic<int> send_error{0};
  bool delivered = false;
  bool timed_out = false;
  uv::raw::timer deadline{loop};
  EXPECT_TRUE(deadline.start(5s, [&](uv::raw::timer &self) {
    timed_out = true;
    if (producer.joinable()) producer.join();
    wakeup.close();
    self.close();
  }));
  EXPECT_TRUE(wakeup.set_callback([&](async &self) {
    EXPECT_EQ(std::this_thread::get_id(), loop_thread);
    producer.join(); // Every send has returned before close begins.
    delivered = true;
    deadline.close();
    self.close();
  }));
  producer = std::jthread{[&] { send_error.store(wakeup.send().error().native()); }};
  loop.run();
  EXPECT_FALSE(timed_out);
  EXPECT_TRUE(delivered);
  EXPECT_EQ(send_error.load(), 0);
}

struct queue_context {
  std::mutex mutex;
  std::vector<int> pending;
  int consumed = 0;
  int sum = 0;
  int notifications = 0;
  std::thread::id loop_thread = std::this_thread::get_id();
};

void drain_queue(async &self) {
  auto &state = *self.user_data<queue_context>();
  EXPECT_EQ(std::this_thread::get_id(), state.loop_thread);
  std::lock_guard lock{state.mutex};
  ++state.notifications;
  for (auto value : state.pending) {
    ++state.consumed;
    state.sum += value;
  }
  state.pending.clear();
}

TEST_F(RawAsync, concurrentProducersPublishThroughApplicationMutexAndJoinBeforeClose) {
  constexpr int producer_count = 4;
  constexpr int sends_per_producer = 250;
  constexpr int total = producer_count * sends_per_producer;
  queue_context state;
  wakeup.user_data(state);
  ASSERT_TRUE(wakeup.set_callback(drain_queue));
  std::atomic<int> completed{0};
  std::atomic<int> errors{0};
  std::vector<std::jthread> producers;
  for (int index = 0; index < producer_count; ++index) {
    producers.emplace_back([&, index] {
      for (int item = 0; item < sends_per_producer; ++item) {
        {
          std::lock_guard lock{state.mutex};
          state.pending.push_back(index * sends_per_producer + item);
        }
        if (!wakeup.send()) errors.fetch_add(1);
      }
      completed.fetch_add(1);
    });
  }
  pump_until([&] { return completed.load() == producer_count; });
  for (auto &producer : producers) producer.join();
  ASSERT_TRUE(wakeup.send()); // Drain any items left after the last coalesced wakeup.
  tick();
  EXPECT_EQ(errors.load(), 0);
  EXPECT_EQ(state.consumed, total);
  EXPECT_EQ(state.sum, total * (total - 1) / 2);
  EXPECT_GT(state.notifications, 0);
  EXPECT_TRUE(state.pending.empty());
  wakeup.close();
}

TEST_F(RawAsync, nativeQueriesReturnExplicitResults) {
  uv_os_fd_t fd{};
  const auto native_status = uv_fileno(wakeup.native_handle(), &fd);
  auto result = wakeup.fileno();
  EXPECT_EQ(result.error(), uv::make_error_code(native_status));
  if (result) {
    EXPECT_EQ(result.value(), fd);
  }
  int size = 0;
  const auto send_status = uv_send_buffer_size(wakeup.native_handle(), &size);
  EXPECT_EQ(wakeup.send_buffer_size().error(), uv::make_error_code(send_status));
  const auto receive_status = uv_recv_buffer_size(wakeup.native_handle(), &size);
  EXPECT_EQ(wakeup.receive_buffer_size().error(), uv::make_error_code(receive_status));
}

struct throwing_copy_callback {
  throwing_copy_callback() = default;
  throwing_copy_callback(const throwing_copy_callback&) {
    throw std::runtime_error{"callable preparation failed"};
  }
  void operator()(async&) const {}
};

TEST(RawAsyncConstruction, callablePreparationFailureDoesNotInitializeNativeHandle) {
  uv::loop loop;
  throwing_copy_callback callback;
  EXPECT_THROW((async{loop, callback}), std::runtime_error);
  EXPECT_THROW((async{loop.view(), callback}), std::runtime_error);
  EXPECT_TRUE(loop.handles().empty());
  loop.close();
}

TEST_F(RawAsync, failedCallablePreparationPreservesPendingStaticCallback) {
  int calls = 0;
  wakeup.user_data(calls);
  ASSERT_TRUE(wakeup.set_callback_static<count_notification>());
  ASSERT_TRUE(wakeup.send());
  throwing_copy_callback callback;
  EXPECT_THROW(wakeup.set_callback(callback), std::runtime_error);
  tick();
  EXPECT_EQ(calls, 1);
}

TEST_F(RawAsync, failedReentrantCallablePreparationRestoresRuntimeCallback) {
  int calls = 0;
  ASSERT_TRUE(wakeup.set_callback([&, count = 0](async &self) mutable {
    calls = ++count;
    if (count == 1) {
      throwing_copy_callback callback;
      EXPECT_THROW(self.set_callback(callback), std::runtime_error);
      EXPECT_TRUE(self.send());
    } else {
      EXPECT_EQ(count, 2);
      self.close();
    }
  }));
  ASSERT_TRUE(wakeup.send());
  pump_until([&] { return wakeup.closing(); });
  EXPECT_EQ(calls, 2);
}

void throwing_notification(async&) {
  throw std::runtime_error{"callback failure"};
}

void run_throwing_runtime_callback() {
  uv::loop loop;
  async wakeup{loop, throwing_notification};
  wakeup.send().value();
  loop.run();
}

void run_throwing_static_callback() {
  uv::loop loop;
  async wakeup{loop, async::static_callback<throwing_notification>{}};
  wakeup.send().value();
  loop.run();
}

TEST(RawAsyncDeathTest, runtimeExceptionsTerminateAtNativeBoundary) {
  EXPECT_DEATH(run_throwing_runtime_callback(), "");
}

TEST(RawAsyncDeathTest, staticExceptionsTerminateAtNativeBoundary) {
  EXPECT_DEATH(run_throwing_static_callback(), "");
}

} // namespace
