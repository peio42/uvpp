#include <atomic>
#include <mutex>
#include <type_traits>

#include "uvpp/core/version.hpp"
#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

std::atomic<int> *once_counter = nullptr;

void increment_once_counter() {
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
  EXPECT_FALSE(mutex.try_lock());
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

  lock.lock_read();
  EXPECT_TRUE(lock.try_lock_read());
  lock.unlock_read();
  lock.unlock_read();

  lock.lock_write();
  EXPECT_FALSE(lock.try_lock_read());
  EXPECT_FALSE(lock.try_lock_write());
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

#if UVPP_HAS_THREAD_DETACH
TEST(Uvpp2Threading, threadCanDetachExplicitly) {
  uv::semaphore finished;

  {
    uv::thread thread{[&] {
      finished.post();
    }};

    EXPECT_TRUE(thread.joinable());
    thread.detach();
    EXPECT_FALSE(thread.joinable());
  }

  finished.wait();
}
#endif

TEST(Uvpp2Threading, threadSelfCanBeCompared) {
  const auto current = uv::thread::self();
  EXPECT_TRUE(uv::thread::equal(current, current));
}
