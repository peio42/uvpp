#include <array>
#include <atomic>
#include <cstddef>
#include <span>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

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

TEST(Uvpp2Random, randomFillSynchronouslyFillsBytes) {
  std::array<std::byte, 32> bytes{};

  EXPECT_NO_THROW(uv::random_fill(std::span<std::byte>{bytes}));
  EXPECT_FALSE(uv::try_random_fill(std::span<std::byte>{bytes}));
}

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

#endif
