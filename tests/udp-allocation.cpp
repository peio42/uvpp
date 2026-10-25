#include <array>
#include <chrono>
#include <cstdlib>
#include <new>

#include "gtest/gtest.h"
#include "uvpp/handles/udp.hpp"
#include "uvpp/handles/tcp.hpp"
#include "uvpp/handles/pipe.hpp"
#include "platform-support.hpp"
#include "uvpp/net/tcp_listener.hpp"
#include "uvpp/net/pipe_listener.hpp"
#include "uvpp/net/udp_socket.hpp"

// This file has its own test executable so allocator replacement cannot affect
// the regular suite. Injection is limited to the tested operation on this thread.
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

namespace {
template<class Create>
void expect_creation_bad_alloc(uv::loop &loop, Create create, int fail_after = 0) {
  bool caught = false;
  {
    allocation_failure injection{fail_after};
    try { create(); }
    catch (const std::bad_alloc &) { caught = true; }
  }
  EXPECT_TRUE(caught);
  int handles = 0;
  uv_walk(loop.native(), [](uv_handle_t *, void *data) {
    ++*static_cast<int *>(data);
  }, &handles);
  EXPECT_EQ(handles, 0); // All C++ preparation precedes native initialization.
}
}

TEST(NetworkFactoriesAllocation, CppFailuresThrowInBothCreationPolicies) {
  uv::loop loop;
  const uv::ipv4 address{"127.0.0.1", 0};
  const std::string path(256, 'p'); // Force allocation in the legacy bind path.
  expect_creation_bad_alloc(loop, [&] { uv::tcp_listener owner{loop, address}; });
  expect_creation_bad_alloc(loop, [&] { (void)uv::ops::make_tcp_listener(loop, address); });
  expect_creation_bad_alloc(loop, [&] { uv::udp_socket owner{loop, address}; });
  expect_creation_bad_alloc(loop, [&] { (void)uv::ops::make_udp_socket(loop, address); });
  expect_creation_bad_alloc(loop, [&] { uv::pipe_listener owner{loop, path}; });
  expect_creation_bad_alloc(loop, [&] { (void)uv::ops::make_pipe_listener(loop, path); });
#if !UVPP_HAS_PIPE_BIND2
  // Cover both path-copy and state-storage failure, with no native handle left.
  expect_creation_bad_alloc(loop, [&] { uv::pipe_listener owner{loop, path}; }, 1);
  expect_creation_bad_alloc(loop, [&] { (void)uv::ops::make_pipe_listener(loop, path); }, 1);
#endif
  EXPECT_NO_THROW(loop.close());
}

// The isolated allocation binary also verifies the raw stream substrate: even
// heap-backed prepared callables must move through native delivery without copies.
TEST(RawStreamAllocation, preparedSubscriptionsAndRepeatedDeliveryNeedNoCppAllocation) {
  uv::loop loop;
  uv::raw::tcp listener{loop}, client{loop}, peer{loop};
  uv::raw::connect_request request;
  bool accepted = false, connected = false;
  listener.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
  listener.listen([&](uv::raw::tcp &self, uv::status status) {
    EXPECT_TRUE(status);
    EXPECT_TRUE(self.accept(peer));
    accepted = true;
  }).throw_if_error();
  client.connect(request, listener.sockname().value().to_v4(), [&](uv::raw::connect_request&, uv::status status) {
    EXPECT_TRUE(status);
    connected = true;
  }).throw_if_error();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!(accepted && connected) && std::chrono::steady_clock::now() < deadline)
    loop.run(uv::run_mode::nowait);
  if (!(accepted && connected)) {
    listener.close(); client.close(); peer.close(); loop.run();
    FAIL() << "TCP pair setup timed out";
  }
  using substrate = uv::raw::stream<uv::raw::tcp, uv_tcp_t>;
  std::array<char, 8> storage{};
  std::array<char, 256> capture{};
  int delivered = 0;
  substrate::allocate_callback allocator = [&, capture](uv::raw::tcp&, std::size_t) {
    storage[0] = capture[0];
    return uv::buffer_view{storage.data(), storage.size()};
  };
  substrate::read_callback reader = [&, capture](uv::raw::tcp&, uv::read_result) { delivered += 1 + capture[0]; };
  uv::raw::write_request writing;
  int write_deliveries = 0;
  bool write_succeeded = false;
  uv::raw::write_request::callback written = [&, capture](uv::raw::write_request &request, uv::status status) {
    write_deliveries += 1 + capture[0];
    write_succeeded = status.has_value() && !request.pending();
  };
  uv::status started, stopped, submitted;
  {
    allocation_failure no_cpp_allocation{0};
    started = client.read_start(std::move(allocator), std::move(reader));
    if (started) {
      auto *native = client.native_stream();
      for (int i = 0; i < 1000; ++i) {
        uv_buf_t buffer{};
        native->alloc_cb(client.native_handle(), storage.size(), &buffer);
        native->read_cb(native, 0, &buffer);
      }
      stopped = client.read_stop();
      submitted = client.write(writing, uv::buffer_view{storage.data(), 1}, std::move(written));
      const auto completion_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
      while (writing.pending() && std::chrono::steady_clock::now() < completion_deadline)
        loop.run(uv::run_mode::nowait);
    }
  }
  EXPECT_TRUE(started);
  EXPECT_TRUE(stopped);
  EXPECT_EQ(delivered, 1000);
  EXPECT_TRUE(submitted);
  EXPECT_EQ(write_deliveries, 1);
  EXPECT_TRUE(write_succeeded);
  listener.close(); client.close(); peer.close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(RawPipeAllocation, nativeResultsAndCppNameAllocationFailuresRemainSeparate) {
  uv::loop loop;
  uv::raw::pipe handle{loop};
  const auto path = uvpp::test::pipe_path("raw-name-allocation");
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  ASSERT_TRUE(handle.bind(path));
  const auto *native = handle.native();
  bool allocation_failed = false;
  {
    allocation_failure injection{0};
    try { (void)handle.sockname(); }
    catch (const std::bad_alloc&) { allocation_failed = true; }
  }
  EXPECT_TRUE(allocation_failed);
  EXPECT_EQ(handle.native(), native);
  EXPECT_TRUE(handle.initialized());
  EXPECT_FALSE(handle.closing());
  EXPECT_EQ(handle.sockname().value(), path);
#if !UVPP_HAS_PIPE_CONNECT2
  uv::raw::pipe client{loop};
  uv::raw::connect_request request;
  uv::raw::connect_request::callback callback = [](uv::raw::connect_request&, uv::status) {};
  bool preparation_failed = false;
  {
    allocation_failure injection{0};
    try { (void)client.connect(request, path, std::move(callback)); }
    catch (const std::bad_alloc&) { preparation_failed = true; }
  }
  EXPECT_TRUE(preparation_failed);
  EXPECT_FALSE(request.pending());
  client.close();
#endif
  handle.close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path, ignored);
}

TEST(RawUdpAllocation, multicastPreparationFailuresRemainCppExceptions) {
  uv::loop loop;
  uv::raw::udp handle{loop};
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  const auto port = handle.sockname().value().port();
  auto *native = handle.native();
  int marker = 42;
  handle.user_data(marker);
  const std::string long_address(128, 'x');
#if UVPP_HAS_UDP_SOURCE_MEMBERSHIP
  constexpr int operations = 3;
#else
  constexpr int operations = 2;
#endif
  for (int operation = 0; operation < operations; ++operation) {
    bool failed = false;
    {
      allocation_failure injection{0};
      try {
        if (operation == 0) (void)handle.set_membership(long_address, uv::membership::join);
        if (operation == 1) (void)handle.set_multicast_interface(long_address);
#if UVPP_HAS_UDP_SOURCE_MEMBERSHIP
        if (operation == 2) (void)handle.set_source_membership("239.255.42.99", long_address, uv::membership::join);
#endif
      } catch (const std::bad_alloc&) { failed = true; }
    }
    EXPECT_TRUE(failed);
    EXPECT_TRUE(handle.initialized());
    EXPECT_FALSE(handle.closing());
    EXPECT_EQ(handle.native(), native);
    EXPECT_EQ(handle.user_data<int>(), &marker);
    EXPECT_EQ(handle.sockname().value().port(), port);
  }
  EXPECT_EQ(handle.set_membership(long_address, uv::membership::join).error(), uv::make_error_code(UV_EINVAL));
  handle.close(); loop.run();
  EXPECT_NO_THROW(loop.close());
}
