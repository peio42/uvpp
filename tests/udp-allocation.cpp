#include <array>
#include <cstdlib>
#include <new>

#include "gtest/gtest.h"
#include "uvpp/handles/udp.hpp"

// This file has its own test executable so allocator replacement cannot affect
// the regular suite. Injection is limited to the assignment on this thread.
namespace {
thread_local int allocations_before_failure = -1;

class allocation_failure {
public:
  explicit allocation_failure(int successful_allocations) {
    allocations_before_failure = successful_allocations;
  }
  ~allocation_failure() { allocations_before_failure = -1; }
  allocation_failure(const allocation_failure &) = delete;
  allocation_failure &operator=(const allocation_failure &) = delete;
};
}

void *operator new(std::size_t size) {
  if (allocations_before_failure == 0) {
    allocations_before_failure = -1;
    throw std::bad_alloc{};
  }
  if (allocations_before_failure > 0) --allocations_before_failure;
  if (void *ptr = std::malloc(size == 0 ? 1 : size)) return ptr;
  throw std::bad_alloc{};
}

// Forward the other unaligned forms as well, including Google Test's nothrow
// allocations, so sanitizer allocation/deallocation tracking stays consistent.
void *operator new[](std::size_t size) { return ::operator new(size); }
void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
  try { return ::operator new(size); } catch (...) { return nullptr; }
}
void *operator new[](std::size_t size, const std::nothrow_t &tag) noexcept {
  return ::operator new(size, tag);
}
void operator delete[](void *ptr) noexcept { std::free(ptr); }
void operator delete[](void *ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete(void *ptr, const std::nothrow_t &) noexcept { std::free(ptr); }
void operator delete[](void *ptr, const std::nothrow_t &) noexcept { std::free(ptr); }

void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete(void *ptr, std::size_t) noexcept { std::free(ptr); }

TEST(Uvpp2Udp, sendBatchCopyAssignmentPreservesStateOnAllocationFailure) {
#if UVPP_HAS_UDP_TRY_SEND2
  uv::ipv4 original_address{"127.0.0.1", 1234};
  uv::ipv4 source_address{"127.0.0.1", 5678};
  std::array original_payload{'o', 'l', 'd'};
  std::array source_payload{'n', 'e', 'w', '!'};
  uv::buffer_view original_buffer{original_payload.data(), original_payload.size()};
  std::array source_buffers{
      uv::buffer_view{source_payload.data(), 2},
      uv::buffer_view{source_payload.data() + 2, 2}};
  uv::udp_send_batch source;
  for (int i = 0; i < 20; ++i) source.add(source_buffers, source_address);

  // Visit every allocation in the copy, including rebuilding pointer arrays,
  // without fixing the test to the current number of internal vectors.
  for (int fail_after = 0; fail_after < 32; ++fail_after) {
    SCOPED_TRACE(fail_after);
    uv::udp_send_batch destination;
    destination.add(original_buffer, original_address);
    const auto before = destination.view();
    const auto *original_native_buffer = before.buffers()[0];
    bool failed = false;
    {
      allocation_failure injection{fail_after};
      try {
        destination = source;
      } catch (const std::bad_alloc &) {
        failed = true;
      }
    }
    if (failed) {
      const auto after = destination.view();
      ASSERT_EQ(after.count(), 1u);
      ASSERT_EQ(after.buffers(), before.buffers());
      ASSERT_EQ(after.buffer_counts(), before.buffer_counts());
      ASSERT_EQ(after.addresses(), before.addresses());
      ASSERT_EQ(after.buffers()[0], original_native_buffer);
      ASSERT_EQ(after.buffer_counts()[0], 1u);
      EXPECT_EQ(after.addresses()[0], original_address.native_sockaddr());
      EXPECT_EQ(after.buffers()[0][0].base, original_payload.data());
      EXPECT_EQ(after.buffers()[0][0].len, original_payload.size());
      // A failed copy must also leave the destination reusable.
      destination = source;
    }
    const auto copied = destination.view();
    ASSERT_EQ(copied.count(), 20u);
    for (unsigned int i = 0; i < copied.count(); ++i) {
      ASSERT_EQ(copied.buffer_counts()[i], 2u);
      EXPECT_EQ(copied.addresses()[i], source_address.native_sockaddr());
      EXPECT_NE(copied.buffers()[i], source.view().buffers()[i]);
      EXPECT_EQ(copied.buffers()[i][0].base, source_payload.data());
      EXPECT_EQ(copied.buffers()[i][0].len, 2u);
      EXPECT_EQ(copied.buffers()[i][1].base, source_payload.data() + 2);
      EXPECT_EQ(copied.buffers()[i][1].len, 2u);
    }
    if (!failed) {
      EXPECT_GE(fail_after, 2); // Includes the original second-allocation bug.
      return;
    }
  }
  FAIL() << "Copy assignment did not succeed within the allocation budget";
#else
  GTEST_SKIP() << "Requires UVPP_HAS_UDP_TRY_SEND2";
#endif
}
