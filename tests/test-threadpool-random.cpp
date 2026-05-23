#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

std::size_t configured_threadpool_size() {
  constexpr std::size_t default_size = 4;
  constexpr std::size_t max_test_size = 32;

  const char *raw = std::getenv("UV_THREADPOOL_SIZE");
  if (!raw || *raw == '\0') {
    return default_size;
  }

  char *end = nullptr;
  const auto value = std::strtoul(raw, &end, 10);
  if (end == raw || value == 0 || value > max_test_size) {
    return 0;
  }

  return value;
}

struct static_work_state {
  std::atomic<bool> worked{false};
  bool after = false;
  int status = UV_EINVAL;
};

void static_work_callback(uv::work_request &request) {
  request.user_data<static_work_state>()->worked.store(true);
}

void static_after_work_callback(uv::work_request &request, uv::result status) {
  auto *state = request.user_data<static_work_state>();
  state->after = true;
  state->status = status.status();
}

class threadpool_blockers {
public:
  threadpool_blockers(uv::loop &loop, std::size_t count) {
    requests_.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
      auto request = std::make_unique<uv::work_request>();
      uv::queue_work(loop, *request,
        [&](uv::work_request &) {
          started_.fetch_add(1);
          while (!released_.load()) {
            std::this_thread::yield();
          }
        },
        [](uv::work_request &, uv::result status) {
          EXPECT_TRUE(status);
        });
      requests_.push_back(std::move(request));
    }
  }

  bool wait_until_started(std::size_t count) const {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (started_.load() != count && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    return started_.load() == count;
  }

  void release() noexcept {
    released_.store(true);
  }

private:
  std::vector<std::unique_ptr<uv::work_request>> requests_;
  std::atomic<bool> released_{false};
  std::atomic<std::size_t> started_{0};
};

#if UVPP_HAS_RANDOM
struct static_random_state {
  bool called = false;
  bool ok = false;
  std::size_t size = 0;
};

void static_random_callback(uv::random_request &request, uv::random_result result) {
  auto *state = request.user_data<static_random_state>();
  state->called = true;
  state->ok = result.ok();
  state->size = result.bytes().size();
}
#endif

}

TEST(Uvpp2Threadpool, queueWorkRunsWorkerAndAfterCallbacks) {
  uv::loop loop;
  uv::work_request request;

  std::atomic<bool> worked{false};
  bool after = false;
  int status = UV_EINVAL;

  uv::queue_work(loop, request,
    [&](uv::work_request &callback_request) {
      if (&callback_request == &request) {
        worked.store(true);
      }
    },
    [&](uv::work_request &callback_request, uv::result callback_status) {
      EXPECT_EQ(&request, &callback_request);
      after = true;
      status = callback_status.status();
    });

  loop.run();

  EXPECT_TRUE(worked.load());
  EXPECT_TRUE(after);
  EXPECT_EQ(status, 0);
  EXPECT_EQ(request.type(), uv::request_type::work);

  loop.close();
}

TEST(Uvpp2Threadpool, queueWorkCancelAfterCompletionFails) {
  uv::loop loop;
  uv::work_request request;

  uv::queue_work(loop, request,
    [](uv::work_request &) {},
    [](uv::work_request &, uv::result) {});

  loop.run();

  EXPECT_THROW(request.cancel(), uv::error);
  auto ec = request.try_cancel();
  ASSERT_TRUE(ec);
  EXPECT_EQ(ec.value(), UV_EBUSY);

  loop.close();
}

TEST(Uvpp2Threadpool, queueWorkCancelPendingCompletesWithCanceledStatus) {
  const auto blocker_count = configured_threadpool_size();
  if (blocker_count == 0) {
    GTEST_SKIP() << "UV_THREADPOOL_SIZE is outside the bounded test range";
  }

  uv::loop loop;
  threadpool_blockers blockers{loop, blocker_count};

  if (!blockers.wait_until_started(blocker_count)) {
    blockers.release();
    loop.run();
    loop.close();
    GTEST_SKIP() << "could not occupy every libuv worker thread";
  }

  uv::work_request target;
  std::atomic<bool> target_started{false};
  bool target_after = false;
  bool target_canceled = false;

  uv::queue_work(loop, target,
    [&](uv::work_request &) {
      target_started.store(true);
    },
    [&](uv::work_request &, uv::result status) {
      target_after = true;
      target_canceled = status.canceled();
    });

  EXPECT_FALSE(target.try_cancel());

  blockers.release();
  loop.run();

  EXPECT_FALSE(target_started.load());
  EXPECT_TRUE(target_after);
  EXPECT_TRUE(target_canceled);

  loop.close();
}

TEST(Uvpp2Threadpool, queueWorkSupportsStaticCallbacks) {
  uv::loop loop;
  uv::work_request request;
  static_work_state state;
  request.user_data(state);

  uv::queue_work_static<static_work_callback, static_after_work_callback>(loop, request);
  loop.run();

  EXPECT_TRUE(state.worked.load());
  EXPECT_TRUE(state.after);
  EXPECT_EQ(state.status, 0);
  EXPECT_EQ(request.type(), uv::request_type::work);

  loop.close();
}

#if UVPP_HAS_RANDOM

TEST(Uvpp2Random, randomFillCompletesAsynchronously) {
  uv::loop loop;
  uv::random_request request;
  std::array<std::byte, 32> bytes{};

  bool called = false;
  bool ok = false;
  std::size_t size = 0;

  uv::random_fill(loop, request, std::span<std::byte>{bytes},
    [&](uv::random_request &callback_request, uv::random_result result) {
      EXPECT_EQ(&request, &callback_request);
      called = true;
      ok = result.ok();
      size = result.bytes().size();
    });

  loop.run();

  EXPECT_TRUE(called);
  EXPECT_TRUE(ok);
  EXPECT_EQ(size, bytes.size());
  EXPECT_EQ(request.type(), uv::request_type::random);

  loop.close();
}

TEST(Uvpp2Random, randomFillSupportsStaticCallbacks) {
  uv::loop loop;
  uv::random_request request;
  std::array<std::byte, 16> bytes{};
  static_random_state state;
  request.user_data(state);

  uv::random_fill_static<static_random_callback>(loop, request, std::span<std::byte>{bytes});
  loop.run();

  EXPECT_TRUE(state.called);
  EXPECT_TRUE(state.ok);
  EXPECT_EQ(state.size, bytes.size());
  EXPECT_EQ(request.type(), uv::request_type::random);

  loop.close();
}

TEST(Uvpp2Random, randomFillCancelPendingCompletesWithCanceledStatus) {
  const auto blocker_count = configured_threadpool_size();
  if (blocker_count == 0) {
    GTEST_SKIP() << "UV_THREADPOOL_SIZE is outside the bounded test range";
  }

  uv::loop loop;
  threadpool_blockers blockers{loop, blocker_count};

  if (!blockers.wait_until_started(blocker_count)) {
    blockers.release();
    loop.run();
    loop.close();
    GTEST_SKIP() << "could not occupy every libuv worker thread";
  }

  uv::random_request target;
  std::array<std::byte, 32> bytes{};
  bool target_after = false;
  bool target_canceled = false;

  uv::random_fill(loop, target, std::span<std::byte>{bytes},
    [&](uv::random_request &, uv::random_result result) {
      target_after = true;
      target_canceled = result.status().canceled();
    });

  EXPECT_FALSE(target.try_cancel());

  blockers.release();
  loop.run();

  EXPECT_TRUE(target_after);
  EXPECT_TRUE(target_canceled);

  loop.close();
}

TEST(Uvpp2Random, randomFillCancelAfterCompletionFails) {
  uv::loop loop;
  uv::random_request request;
  std::array<std::byte, 32> bytes{};

  uv::random_fill(loop, request, std::span<std::byte>{bytes},
    [](uv::random_request &, uv::random_result) {});

  loop.run();

  EXPECT_THROW(request.cancel(), uv::error);
  auto ec = request.try_cancel();
  ASSERT_TRUE(ec);
  EXPECT_EQ(ec.value(), UV_EBUSY);

  loop.close();
}

#endif
