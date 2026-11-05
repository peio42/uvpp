#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/threadpool/work.hpp"

using namespace std::chrono_literals;

namespace {
using request_type = uv::raw::work_request;

bool wait_until(const std::atomic<bool> &flag) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!flag.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return flag.load();
}

struct transition_state {
  uv::loop *loop;
  std::atomic<int> worker_calls{0};
  int after_calls = 0;
};

void static_work(request_type &request) {
  ++request.user_data<transition_state>()->worker_calls;
}
void static_after(request_type &request, uv::status outcome) {
  auto &state = *request.user_data<transition_state>();
  EXPECT_TRUE(outcome);
  EXPECT_FALSE(request.pending());
  ++state.after_calls;
  EXPECT_TRUE(uv::raw::queue_work(*state.loop, request,
    [&state](request_type &) { ++state.worker_calls; },
    [&state](request_type &self, uv::status result) {
      EXPECT_TRUE(result);
      EXPECT_FALSE(self.pending());
      ++state.after_calls;
    }));
}
}

TEST(RawWork, workerAndCompletionUseTheirRespectiveThreadsAndRetainRequest) {
  static_assert(!std::is_copy_constructible_v<request_type>);
  static_assert(!std::is_move_constructible_v<request_type>);
  uv::loop loop;
  request_type request;
  int marker = 17;
  request.user_data(marker);
  const auto loop_thread = std::this_thread::get_id();
  std::thread::id worker_thread;
  std::atomic<bool> started{false}, release{false}, finished{false};
  bool after = false;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(uv::raw::queue_work(loop, request,
    [&, token](request_type &self) {
      worker_thread = std::this_thread::get_id();
      EXPECT_EQ(self.user_data<int>(), &marker);
      started.store(true);
      while (!release.load()) std::this_thread::yield();
      finished.store(true);
    },
    [&](request_type &self, uv::status status) {
      EXPECT_EQ(std::this_thread::get_id(), loop_thread);
      EXPECT_FALSE(self.pending());
      EXPECT_TRUE(status);
      EXPECT_TRUE(weak.expired());
      after = true;
    }));
  token.reset();
  const bool has_started = wait_until(started);
  EXPECT_TRUE(has_started);
  if (has_started) {
    EXPECT_TRUE(request.pending());
    EXPECT_FALSE(weak.expired());
    EXPECT_EQ(request.cancel().error(), uv::make_error_code(UV_EBUSY));
    EXPECT_EQ(uv::raw::queue_work(loop, request, [](auto&) {}, [](auto&, auto) {}).error(),
              uv::make_error_code(UV_EBUSY));
  }
  release.store(true);
  if (has_started) {
    EXPECT_TRUE(wait_until(finished));
    EXPECT_EQ(request.cancel().error(), uv::make_error_code(UV_EBUSY));
  }
  EXPECT_TRUE(request.pending()); // Completion has not yet run on the loop.
  loop.run();
  EXPECT_TRUE(after);
  EXPECT_NE(worker_thread, loop_thread);
  EXPECT_FALSE(request.pending());
  EXPECT_EQ(request.user_data<int>(), &marker);
  loop.close();
}

TEST(RawWork, callbackFormsCanBeChangedFromCompletion) {
  uv::loop loop;
  request_type request;
  transition_state state{&loop};
  request.user_data(state);
  ASSERT_TRUE(uv::raw::queue_work(loop, request,
    [&state](request_type &) { ++state.worker_calls; },
    [&state](request_type &self, uv::status result) {
      EXPECT_TRUE(result);
      EXPECT_FALSE(self.pending());
      ++state.after_calls;
      EXPECT_TRUE((uv::raw::queue_work_static<static_work, static_after>(*state.loop, self)));
    }));
  loop.run();
  EXPECT_EQ(state.worker_calls.load(), 3);
  EXPECT_EQ(state.after_calls, 3);
  EXPECT_FALSE(request.pending());
  loop.close();
}

TEST(RawWork, invalidCallbacksLeaveRequestReusableAndReleaseCaptures) {
  uv::loop loop;
  request_type request;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  const auto result = uv::raw::queue_work(loop, request,
    [token](request_type &) {}, {});
  token.reset();
  EXPECT_EQ(result.error(), uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(request.pending());
  bool done = false;
  ASSERT_TRUE(uv::raw::queue_work(loop, request, [](request_type &) {},
    [&](request_type &, uv::status status) { EXPECT_TRUE(status); done = true; }));
  loop.run();
  EXPECT_TRUE(done);
  loop.close();
}

TEST(RawWork, completionCanDestroyRequestStorage) {
  uv::loop loop;
  auto request = std::make_unique<request_type>();
  bool done = false;
  ASSERT_TRUE(uv::raw::queue_work(loop, *request, [](request_type &) {},
    [&](request_type &self, uv::status status) {
      EXPECT_TRUE(status);
      EXPECT_FALSE(self.pending());
      EXPECT_EQ(&self, request.get());
      request.reset();
      done = true;
    }));
  loop.run();
  EXPECT_TRUE(done);
  EXPECT_EQ(request, nullptr);
  loop.close();
}
