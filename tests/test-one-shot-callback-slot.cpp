#include <memory>

#include <gtest/gtest.h>

#include "uvpp/detail/one_shot_callback_slot.hpp"

namespace {
using slot = uv::detail::one_shot_callback_slot<int>;

TEST(UvppOneShotCallbackSlot, claimsAndReleasesOnlyForTheActiveOwner) {
  slot waiter;
  int first = 0;
  int second = 0;
  auto deliver = +[](void *context, int value) noexcept {
    *static_cast<int *>(context) = value;
  };
  EXPECT_FALSE(waiter.claimed_by(nullptr));
  EXPECT_FALSE(waiter.release(nullptr));
  EXPECT_FALSE(waiter.claim(nullptr, deliver));
  EXPECT_FALSE(waiter.claimed());
  EXPECT_FALSE(waiter.claim(&first, nullptr));
  EXPECT_FALSE(waiter.claimed());
  waiter.deliver(1);
  EXPECT_EQ(first, 0);
  EXPECT_TRUE(waiter.claim(&first, deliver));
  EXPECT_TRUE(waiter.claimed_by(&first));
  EXPECT_FALSE(waiter.claim(nullptr, deliver));
  EXPECT_FALSE(waiter.claim(&first, nullptr));
  EXPECT_TRUE(waiter.claimed_by(&first));
  EXPECT_FALSE(waiter.claim(&second, deliver));
  EXPECT_FALSE(waiter.release(&second));
  waiter.deliver(7);
  waiter.deliver(9);
  EXPECT_EQ(first, 7);
  EXPECT_EQ(second, 0);
  EXPECT_FALSE(waiter.claimed());
  EXPECT_TRUE(waiter.claim(&second, deliver));
  EXPECT_TRUE(waiter.release(&second));
  waiter.deliver(11);
  EXPECT_EQ(second, 0);
}

TEST(UvppOneShotCallbackSlot, deliveryPreservesAReentrantReplacement) {
  slot waiter;
  struct context {
    slot &waiter;
    int calls = 0;
  } active{waiter};
  ASSERT_TRUE(waiter.claim(&active, +[](void *raw, int) noexcept {
    auto &self = *static_cast<context *>(raw);
    EXPECT_FALSE(self.waiter.claimed());
    EXPECT_FALSE(self.waiter.claimed_by(&self));
    EXPECT_TRUE(self.waiter.claim(&self, +[](void *raw, int value) noexcept {
      static_cast<context *>(raw)->calls += value;
    }));
    ++self.calls;
  }));
  waiter.deliver(0);
  EXPECT_TRUE(waiter.claimed_by(&active));
  waiter.deliver(2);
  EXPECT_EQ(active.calls, 3);
  EXPECT_FALSE(waiter.claimed());
}

TEST(UvppOneShotCallbackSlot, deliveryCanDestroyTheContainingState) {
  auto waiter = std::make_unique<slot>();
  ASSERT_TRUE(waiter->claim(&waiter, +[](void *raw, int) noexcept {
    static_cast<std::unique_ptr<slot> *>(raw)->reset();
  }));
  waiter->deliver(0);
  EXPECT_EQ(waiter, nullptr);
}

TEST(UvppOneShotCallbackSlot, forwardsMoveOnlyPayloadAndReferences) {
  uv::detail::one_shot_callback_slot<std::unique_ptr<int>, int &> waiter;
  int received = 0;
  int borrowed = 0;
  ASSERT_TRUE(waiter.claim(&received, +[](void *raw, std::unique_ptr<int> value,
                                        int &reference) noexcept {
    *static_cast<int *>(raw) = *value;
    reference = *value;
  }));
  waiter.deliver(std::make_unique<int>(42), borrowed);
  EXPECT_EQ(received, 42);
  EXPECT_EQ(borrowed, 42);
}
} // namespace
