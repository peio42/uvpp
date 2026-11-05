#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/misc/random.hpp"

#if UVPP_HAS_RANDOM
namespace {
using request_type = uv::raw::random_request;
using result_type = uv::raw::random_result;

struct transition_state {
  uv::loop *loop;
  std::array<std::byte, 24> first{}, second{}, third{};
  std::weak_ptr<int> first_capture;
  int first_calls = 0, static_calls = 0, last_calls = 0;
};
void static_random(request_type &request, result_type result) {
  auto &state = *request.user_data<transition_state>();
  EXPECT_TRUE(result);
  EXPECT_EQ(result.bytes().data(), state.second.data());
  EXPECT_EQ(result.bytes().size(), state.second.size());
  EXPECT_FALSE(request.pending());
  EXPECT_TRUE(state.first_capture.expired());
  ++state.static_calls;
  EXPECT_TRUE(uv::raw::random_fill(*state.loop, request, state.third,
    [&state](request_type &self, result_type next) {
      EXPECT_TRUE(next);
      EXPECT_FALSE(self.pending());
      EXPECT_EQ(next.bytes().data(), state.third.data());
      ++state.last_calls;
    }));
}
}

TEST(RawRandom, synchronousFillReturnsStatusWithoutRequest) {
  std::array<std::byte, 32> bytes{};
  EXPECT_TRUE(uv::raw::random_fill_now(bytes));
  static_assert(noexcept(uv::raw::random_fill_now(std::span<std::byte>{})));
}

TEST(RawRandom, resultBorrowsTheBufferEvenWhenCompletionFails) {
  std::array<std::byte, 8> bytes{};
  const result_type failure{UV_ECANCELED, bytes.data(), bytes.size()};
  EXPECT_FALSE(failure);
  EXPECT_EQ(failure.error_code(), uv::make_error_code(UV_ECANCELED));
  EXPECT_TRUE(failure.bytes().empty());
  EXPECT_EQ(failure.buffer().data(), bytes.data());
  EXPECT_EQ(failure.buffer().size(), bytes.size());
}

TEST(RawRandom, transitionsCallbacksAndReusesCallerOwnedBuffer) {
  static_assert(!std::is_move_constructible_v<request_type>);
  static_assert(!std::is_copy_constructible_v<request_type>);
  uv::loop loop;
  request_type request;
  transition_state state{};
  state.loop = &loop;
  request.user_data(state);
  auto capture = std::make_shared<int>(1);
  state.first_capture = capture;
  ASSERT_TRUE(uv::raw::random_fill(loop, request, state.first,
    [&, capture](request_type &self, result_type result) {
      EXPECT_TRUE(result);
      EXPECT_EQ(self.user_data<transition_state>(), &state);
      EXPECT_FALSE(self.pending());
      EXPECT_EQ(result.buffer().data(), state.first.data());
      EXPECT_EQ(result.buffer().size(), state.first.size());
      ++state.first_calls;
      EXPECT_TRUE(uv::raw::random_fill_static<static_random>(loop, self, state.second));
    }));
  capture.reset();
  EXPECT_TRUE(request.pending());
  EXPECT_EQ(uv::raw::random_fill(loop, request, state.second, [](auto&, auto) {}).error(),
            uv::make_error_code(UV_EBUSY));
  loop.run();
  EXPECT_EQ(state.first_calls, 1);
  EXPECT_EQ(state.static_calls, 1);
  EXPECT_EQ(state.last_calls, 1);
  EXPECT_TRUE(state.first_capture.expired());
  EXPECT_FALSE(request.pending());
  loop.close();
}

TEST(RawRandom, missingCallbackRejectsSubmissionAndReleasesCapture) {
  uv::loop loop;
  request_type request;
  std::array<std::byte, 16> bytes{};
  EXPECT_EQ(uv::raw::random_fill(loop, request, bytes, {}).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(request.pending());
  auto capture = std::make_shared<int>(1);
  std::weak_ptr<int> weak = capture;
  bool done = false;
  ASSERT_TRUE(uv::raw::random_fill(loop, request, bytes, [&, capture](auto &self, auto result) {
    EXPECT_FALSE(self.pending());
    EXPECT_TRUE(result);
    done = true;
  }));
  capture.reset();
  EXPECT_FALSE(weak.expired());
  loop.run();
  EXPECT_TRUE(done);
  EXPECT_TRUE(weak.expired());
  loop.close();
}

TEST(RawRandom, completionCanDestroyRequest) {
  uv::loop loop;
  auto request = std::make_unique<request_type>();
  std::array<std::byte, 8> bytes{};
  bool done = false;
  ASSERT_TRUE(uv::raw::random_fill(loop, *request, bytes, [&](auto &self, result_type result) {
    EXPECT_TRUE(result);
    EXPECT_FALSE(self.pending());
    EXPECT_EQ(result.bytes().data(), bytes.data());
    request.reset();
    done = true;
  }));
  loop.run();
  EXPECT_TRUE(done);
  EXPECT_EQ(request, nullptr);
  loop.close();
}
#endif
