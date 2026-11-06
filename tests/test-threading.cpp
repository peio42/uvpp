#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include "uvpp/core/version.hpp"
#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

std::atomic<int> *once_counter = nullptr;

void increment_once_counter() noexcept {
  once_counter->fetch_add(1);
}

int static_once_calls = 0;

void increment_static_once_counter() {
  ++static_once_calls;
}

} // namespace

TEST(Uvpp2Threading, exposesNonCopyableNonMovablePrimitives) {
  static_assert(!std::is_copy_constructible_v<uv::mutex>);
  static_assert(!std::is_move_constructible_v<uv::mutex>);
  static_assert(!std::is_copy_constructible_v<uv::recursive_mutex>);
  static_assert(!std::is_move_constructible_v<uv::recursive_mutex>);
  static_assert(!std::is_copy_constructible_v<uv::rwlock>);
  static_assert(!std::is_move_constructible_v<uv::rwlock>);
  static_assert(!std::is_copy_constructible_v<uv::semaphore>);
  static_assert(!std::is_move_constructible_v<uv::semaphore>);
  static_assert(!std::is_copy_constructible_v<uv::condition_variable>);
  static_assert(!std::is_move_constructible_v<uv::condition_variable>);
  static_assert(!std::is_copy_constructible_v<uv::barrier>);
  static_assert(!std::is_move_constructible_v<uv::barrier>);
  static_assert(!std::is_copy_constructible_v<uv::thread_key>);
  static_assert(!std::is_move_constructible_v<uv::thread_key>);
  static_assert(!std::is_copy_constructible_v<uv::once>);
  static_assert(!std::is_move_constructible_v<uv::once>);
  static_assert(!std::is_copy_constructible_v<uv::thread>);
  static_assert(!std::is_move_constructible_v<uv::thread>);
}

TEST(Uvpp2Threading, mutexWorksWithStandardLockUtilities) {
  uv::mutex mutex;
  int value = 0;

  {
    std::lock_guard<uv::mutex> guard{mutex};
    ++value;
  }

  std::unique_lock<uv::mutex> lock{mutex, std::defer_lock};
  EXPECT_TRUE(lock.try_lock());
  EXPECT_EQ(value, 1);
}

TEST(Uvpp2Threading, mutexTryLockReportsBusy) {
  uv::mutex mutex;

  mutex.lock();
  bool acquired = false;
  uv::thread contender{[&] {
    acquired = mutex.try_lock();
    if (acquired) mutex.unlock();
  }};
  contender.join();
  EXPECT_FALSE(acquired);
  mutex.unlock();

  EXPECT_TRUE(mutex.try_lock());
  mutex.unlock();
}

TEST(Uvpp2Threading, recursiveMutexAllowsRepeatedLocking) {
  uv::recursive_mutex mutex;

  mutex.lock();
  EXPECT_TRUE(mutex.try_lock());
  mutex.unlock();
  mutex.unlock();
}

TEST(Uvpp2Threading, rwlockExposesReadAndWriteModes) {
  uv::rwlock lock;

  // Probe contention from another thread. Trying a read/write lock already
  // held by this thread can report EDEADLK on BSD (which libuv aborts on).
  // Native Windows mutexes also permit recursive acquisition.
  bool reader_acquired = false;
  bool writer_acquired = false;
  lock.lock_read();
  uv::thread shared_contender{[&] {
    reader_acquired = lock.try_lock_read();
    if (reader_acquired) lock.unlock_read();
    writer_acquired = lock.try_lock_write();
    if (writer_acquired) lock.unlock_write();
  }};
  shared_contender.join();
  EXPECT_TRUE(reader_acquired);
  EXPECT_FALSE(writer_acquired);
  lock.unlock_read();

  lock.lock_write();
  uv::thread exclusive_contender{[&] {
    reader_acquired = lock.try_lock_read();
    if (reader_acquired) lock.unlock_read();
    writer_acquired = lock.try_lock_write();
    if (writer_acquired) lock.unlock_write();
  }};
  exclusive_contender.join();
  EXPECT_FALSE(reader_acquired);
  EXPECT_FALSE(writer_acquired);
  lock.unlock_write();

  EXPECT_TRUE(lock.try_lock_write());
  lock.unlock_write();
}

TEST(Uvpp2Threading, semaphorePostsAndWaits) {
  uv::semaphore semaphore;

  EXPECT_FALSE(semaphore.try_wait());
  semaphore.post();
  EXPECT_TRUE(semaphore.try_wait());
  EXPECT_FALSE(semaphore.try_wait());

  std::atomic<bool> ran{false};
  uv::thread worker{[&] {
    semaphore.wait();
    ran.store(true);
  }};

  semaphore.post();
  worker.join();
  EXPECT_TRUE(ran.load());
}

TEST(Uvpp2Threading, conditionVariableSignalsWaitingThread) {
  uv::mutex mutex;
  uv::condition_variable condition;
  uv::semaphore waiting;
  bool ready = false;
  std::atomic<bool> observed{false};

  uv::thread worker{[&] {
    mutex.lock();
    waiting.post();
    while (!ready) {
      condition.wait(mutex);
    }
    observed.store(true);
    mutex.unlock();
  }};

  waiting.wait();
  mutex.lock();
  ready = true;
  condition.signal();
  mutex.unlock();

  worker.join();
  EXPECT_TRUE(observed.load());
}

TEST(Uvpp2Threading, conditionVariableTimedWaitReportsTimeout) {
  uv::mutex mutex;
  uv::condition_variable condition;

  mutex.lock();
  EXPECT_FALSE(condition.wait_for(mutex, std::chrono::milliseconds{1}));
  mutex.unlock();
}

TEST(Uvpp2Threading, rejectsCountsThatCannotBeRepresentedNatively) {
  try {
    uv::semaphore invalid{-1};
    ADD_FAILURE() << "A negative semaphore count must be rejected";
  } catch (const uv::error &error) {
    EXPECT_EQ(error.code().value(), UV_EINVAL);
  }
  EXPECT_THROW((uv::barrier{-1}), uv::error);
  if constexpr (std::numeric_limits<unsigned int>::digits < std::numeric_limits<std::uint64_t>::digits) {
    const auto too_large = std::uint64_t{(std::numeric_limits<unsigned int>::max)()} + 1;
    EXPECT_THROW((uv::semaphore{too_large}), uv::error);
    EXPECT_THROW((uv::barrier{too_large}), uv::error);
  }
  EXPECT_THROW((uv::barrier{0}), uv::error);
  uv::semaphore semaphore{0};
  EXPECT_FALSE(semaphore.try_wait());
  uv::barrier barrier{1};
  EXPECT_EQ(barrier.wait(), uv::barrier_wait_result::serial_thread);
}

TEST(Uvpp2Threading, timedWaitConversionHandlesFloatingBoundaries) {
  using nanoseconds = std::chrono::duration<long double, std::nano>;
  const auto max = (std::numeric_limits<std::uint64_t>::max)();
  const auto boundary = std::ldexp(1.0L, 64);
  EXPECT_EQ(uv::detail::condition_timeout_ns(nanoseconds{-1}), 0u);
  EXPECT_EQ(uv::detail::condition_timeout_ns(nanoseconds{0.9L}), 0u);
  EXPECT_EQ(uv::detail::condition_timeout_ns(nanoseconds{1.9L}), 1u);
  EXPECT_EQ(uv::detail::condition_timeout_ns(nanoseconds{boundary}), max);
  EXPECT_EQ(uv::detail::condition_timeout_ns(nanoseconds{std::numeric_limits<long double>::infinity()}), max);
  const auto below_boundary = std::nextafter(boundary, 0.0L);
  EXPECT_EQ(uv::detail::condition_timeout_ns(nanoseconds{below_boundary}),
            static_cast<std::uint64_t>(below_boundary));
  uv::mutex mutex;
  uv::condition_variable condition;
  std::lock_guard<uv::mutex> lock{mutex};
  EXPECT_THROW(condition.wait_for(mutex, nanoseconds{std::numeric_limits<long double>::quiet_NaN()}), uv::error);
  EXPECT_FALSE(condition.wait_for(mutex, nanoseconds{-1}));
}

TEST(Uvpp2Threading, barrierReleasesAllParticipants) {
  uv::barrier barrier{2};
  std::atomic<int> serial_threads{0};

  uv::thread worker{[&] {
    if (barrier.wait() == uv::barrier_wait_result::serial_thread) {
      serial_threads.fetch_add(1);
    }
  }};

  if (barrier.wait() == uv::barrier_wait_result::serial_thread) {
    serial_threads.fetch_add(1);
  }

  worker.join();
  EXPECT_EQ(serial_threads.load(), 1);
}

TEST(Uvpp2Threading, threadKeyStoresThreadLocalPointers) {
  uv::thread_key key;
  int main_value = 1;
  key.set(&main_value);

  std::atomic<bool> worker_initially_empty{false};
  std::atomic<bool> worker_value_visible{false};

  uv::thread worker{[&] {
    int worker_value = 2;
    worker_initially_empty.store(key.get<int>() == nullptr);
    key.set(&worker_value);
    worker_value_visible.store(key.get<int>() == &worker_value);
    key.clear();
  }};

  worker.join();

  EXPECT_TRUE(worker_initially_empty.load());
  EXPECT_TRUE(worker_value_visible.load());
  EXPECT_EQ(key.get<int>(), &main_value);
  key.clear();
  EXPECT_EQ(key.get(), nullptr);
}

TEST(Uvpp2Threading, onceRunsCallbackOnlyOnce) {
  uv::once once;
  std::atomic<int> calls{0};
  once_counter = &calls;

  once.run(increment_once_counter);
  once.run(increment_once_counter);

  EXPECT_EQ(calls.load(), 1);
  once_counter = nullptr;
}

TEST(Uvpp2Threading, onceRunsStaticCallbackOnlyOnce) {
  uv::once once;
  static_once_calls = 0;

  once.run_static<increment_static_once_counter>();
  once.run_static<increment_static_once_counter>();

  EXPECT_EQ(static_once_calls, 1);
}

TEST(Uvpp2Threading, threadRunsAndJoins) {
  std::atomic<bool> ran{false};

  uv::thread thread{[&] {
    ran.store(true);
  }};

  EXPECT_TRUE(thread.joinable());
  thread.join();
  EXPECT_FALSE(thread.joinable());
  EXPECT_TRUE(ran.load());
}

TEST(Uvpp2Threading, threadOwnsMoveOnlyCallableUntilWorkerFinishes) {
  uv::semaphore started, release;
  auto shared = std::make_shared<int>(42);
  std::weak_ptr<int> weak = shared;
  auto value = std::make_unique<int>(7);
  std::atomic<int> observed{0};
  uv::thread thread{[&, owner = std::move(shared), value = std::move(value)] {
    observed.store(*owner + *value);
    started.post();
    release.wait();
  }};
  EXPECT_EQ(value, nullptr);
  started.wait();
  EXPECT_FALSE(weak.expired());
  EXPECT_EQ(observed.load(), 49);
  release.post();
  thread.join();
  EXPECT_TRUE(weak.expired());
}

TEST(Uvpp2Threading, callableCopyFailureLeavesThreadReusable) {
  struct throwing_copy {
    throwing_copy() = default;
    throwing_copy(const throwing_copy &) { throw std::runtime_error{"copy"}; }
    void operator()() const {}
  } callable;
  uv::thread thread;
  EXPECT_THROW(thread.start(callable), std::runtime_error);
  EXPECT_FALSE(thread.joinable());
  std::atomic<bool> ran{false};
  thread.start([&] { ran.store(true); });
  thread.join();
  EXPECT_TRUE(ran.load());
}

TEST(Uvpp2Threading, threadAcceptsCopyOnlyLvalueCallable) {
  struct copy_only {
    std::atomic<bool> *ran;
    explicit copy_only(std::atomic<bool> &value) : ran{&value} {}
    copy_only(const copy_only &) = default;
    copy_only(copy_only &&) = delete;
    void operator()() { ran->store(true); }
  };
  std::atomic<bool> ran{false};
  copy_only callable{ran};
  uv::thread thread{callable};
  thread.join();
  EXPECT_TRUE(ran.load());
}

TEST(Uvpp2Threading, rejectsRestartWhileJoinableAndCanRestartAfterJoin) {
  uv::semaphore started, release;
  uv::thread thread{[&] { started.post(); release.wait(); }};
  started.wait();
  EXPECT_THROW(thread.start([] {}), uv::error);
  EXPECT_TRUE(thread.joinable());
  release.post();
  thread.join();
  std::atomic<bool> reran{false};
  thread.start([&] { reran.store(true); });
  thread.join();
  EXPECT_TRUE(reran.load());
  EXPECT_THROW(thread.join(), uv::error);
}

TEST(Uvpp2ThreadingDeathTest, destroyingJoinableThreadTerminates) {
  EXPECT_DEATH_IF_SUPPORTED({ uv::thread thread{[] {}}; }, "");
}

TEST(Uvpp2ThreadingDeathTest, exceptionInThreadCallbackTerminates) {
  EXPECT_DEATH_IF_SUPPORTED({
    uv::thread thread{[] { throw std::runtime_error{"worker"}; }};
    thread.join();
  }, "");
}

#if UVPP_HAS_THREAD_DETACH
TEST(Uvpp2Threading, detachedWorkerRetainsCallableBeyondWrapperLifetime) {
  auto started = std::make_shared<uv::semaphore>();
  auto release = std::make_shared<uv::semaphore>();
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  {
    uv::thread thread{[started, release, token] {
      started->post();
      release->wait();
    }};
    token.reset();
    started->wait();
    thread.detach();
    EXPECT_FALSE(thread.joinable());
  }
  EXPECT_FALSE(weak.expired());
  release->post();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!weak.expired() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  EXPECT_TRUE(weak.expired());
}

TEST(Uvpp2Threading, threadCanDetachExplicitly) {
  auto finished = std::make_shared<uv::semaphore>();

  {
    uv::thread thread{[finished] {
      finished->post();
    }};

    EXPECT_TRUE(thread.joinable());
    thread.detach();
    EXPECT_FALSE(thread.joinable());
  }

  finished->wait();
}
#endif

TEST(Uvpp2Threading, threadSelfCanBeCompared) {
  const auto current = uv::thread::self();
  EXPECT_TRUE(uv::thread::equal(current, current));
}
