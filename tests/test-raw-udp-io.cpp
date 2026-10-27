#include <array>
#include <chrono>
#include <cstring>
#include <type_traits>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "uvpp/handles/udp.hpp"

namespace {
using udp = uv::raw::udp;
using request = uv::raw::udp_send_request;
struct completion {
  int calls = 0;
  uv::status outcome;
};
void record_send(request &operation, uv::status status) {
  EXPECT_FALSE(operation.pending());
  auto &state = *operation.user_data<completion>();
  ++state.calls;
  state.outcome = status;
}
struct static_receive_state {
  std::array<char, 65536> buffer{};
  int packets = 0;
  int frees = 0;
  bool wait_for_marker = false;
  bool marker_received = false;
};
uv::buffer_view static_allocate(udp &self, std::size_t) {
  auto &buffer = self.user_data<static_receive_state>()->buffer;
  return uv::buffer_view{buffer.data(), buffer.size()};
}
void static_receive(udp &self, uv::udp_receive_result result) {
  auto &state = *self.user_data<static_receive_state>();
  EXPECT_TRUE(result);
  if (result.mmsg_free()) { ++state.frees; return; }
  if (result.empty_event()) return;
  ++state.packets;
  state.marker_received = result.storage()[0] == 'd';
  if (!state.wait_for_marker || state.marker_received) { EXPECT_TRUE(self.receive_stop()); }
}

class RawUdpIO : public ::testing::Test {
protected:
  uv::loop loop;
  udp receiver, sender;
  request operation;
  std::array<char, 65536> buffer{};
  static_receive_state static_state;

  ~RawUdpIO() override {
    receiver.close(); sender.close(); loop.run();
    EXPECT_FALSE(operation.pending());
    EXPECT_NO_THROW(loop.close());
  }
  void setup(bool mmsg = false) {
#if UVPP_HAS_UDP_INIT_EX && UVPP_HAS_UDP_RECVMMSG_FLAG
    if (mmsg) receiver.init(loop, uv::udp_socket_family::ipv4, uv::udp_init_flag::recvmmsg).throw_if_error();
    else receiver.init(loop).throw_if_error();
#else
    (void)mmsg;
    receiver.init(loop).throw_if_error();
#endif
    sender.init(loop).throw_if_error();
    receiver.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
    sender.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
  }
  auto allocate() { return [this](udp&, std::size_t) { return uv::buffer_view{buffer.data(), buffer.size()}; }; }
  uv::ipv4 destination() { return receiver.sockname().value().to_v4(); }
  void send_byte(char value) {
    const auto sent = sender.send_now(uv::buffer_view{&value, 1}, destination());
    ASSERT_TRUE(sent.ok());
    ASSERT_EQ(sent.bytes_sent(), 1u);
  }
  template<class Predicate>
  void until(Predicate done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!done() && std::chrono::steady_clock::now() < end) {
      loop.run(uv::run_mode::nowait);
      std::this_thread::yield();
    }
    if (!done()) {
      ADD_FAILURE() << "Native UDP completion timed out";
      // Settle native requests before their test-body observers go out of scope.
      receiver.close(); sender.close(); loop.run();
    }
  }
};

TEST_F(RawUdpIO, invalidInputDoesNotStartNativeReceiveOrRetainCallbacks) {
  static_assert(!std::is_copy_constructible_v<request>);
  static_assert(!std::is_move_constructible_v<request>);
  static_assert(std::is_same_v<decltype(sender.send(operation, uv::buffer_view{}, record_send)), uv::status>);
  static_assert(noexcept(receiver.receive_stop()));
  setup();
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(receiver.receive_start({}, [token](udp&, uv::udp_receive_result) {}).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(receiver.receive_start([token](udp&, std::size_t) { return uv::buffer_view{}; }, {}).error(), uv::make_error_code(UV_EINVAL));
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(receiver.active());
  EXPECT_TRUE(receiver.receive_stop());
  EXPECT_TRUE(receiver.receive_stop());
  EXPECT_EQ(sender.send(operation, std::span<const uv::buffer_view>{}, destination(), record_send).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(sender.send(operation, uv::buffer_view{nullptr, 1}, destination(), record_send).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(sender.send(operation, uv::buffer_view{}, destination(), {}).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(operation.pending());
  EXPECT_EQ(sender.send_now(std::span<const uv::buffer_view>{}, destination()).error_code(), uv::make_error_code(UV_EINVAL));
}

TEST_F(RawUdpIO, realReceiveKeepsAllocatorOwnedBufferThroughRejectedReplacementAndStop) {
  setup();
  auto owned = std::make_shared<std::array<char, 32>>();
  const auto *address = owned->data();
  std::weak_ptr<std::array<char, 32>> weak = owned;
  int packets = 0;
  ASSERT_TRUE(receiver.receive_start([owned](udp&, std::size_t) { return uv::buffer_view{owned->data(), owned->size()}; },
    [&, count = 0](udp &self, uv::udp_receive_result result) mutable {
      ASSERT_TRUE(result);
      if (result.empty_event()) return;
      packets = ++count;
      EXPECT_EQ(result.storage().data(), address);
      EXPECT_EQ(self.receive_start({}, [](udp&, uv::udp_receive_result) {}).error(), uv::make_error_code(UV_EINVAL));
      if (packets == 2) { EXPECT_TRUE(self.receive_stop()); }
      ASSERT_FALSE(weak.expired());
      EXPECT_EQ(result.storage()[0], packets == 1 ? 'a' : 'b');
    }));
  owned.reset();
  send_byte('a'); until([&] { return packets == 1; });
  EXPECT_FALSE(weak.expired());
  send_byte('b'); until([&] { return packets == 2; });
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(receiver.active());
}

TEST_F(RawUdpIO, realReceiveCanReplaceRuntimeWithStaticFromItsOwnCallback) {
  setup();
  receiver.user_data(static_state);
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  int packets = 0;
  ASSERT_TRUE(receiver.receive_start(allocate(), [&, token](udp &self, uv::udp_receive_result result) {
    ASSERT_TRUE(result);
    if (result.empty_event()) return;
    ++packets;
    EXPECT_TRUE((self.receive_start_static<static_allocate, static_receive>()));
    EXPECT_FALSE(weak.expired());
  }));
  token.reset();
  send_byte('a'); until([&] { return packets == 1; });
  EXPECT_TRUE(weak.expired());
  send_byte('b'); until([&] { return static_state.packets == 1; });
  EXPECT_EQ(packets, 1);
  EXPECT_FALSE(receiver.active());
}

TEST_F(RawUdpIO, nativeEnobufsStopsBeforeDeliveryAndAllowsRearming) {
  setup();
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  bool failed = false, received = false;
  ASSERT_TRUE(receiver.receive_start([](udp&, std::size_t) { return uv::buffer_view{}; },
    [&, token](udp &self, uv::udp_receive_result result) {
      EXPECT_EQ(result.raw_status(), UV_ENOBUFS);
      EXPECT_FALSE(self.active());
      EXPECT_TRUE(self.receive_start(allocate(), [&](udp &current, uv::udp_receive_result next) {
        ASSERT_TRUE(next);
        if (next.empty_event()) return;
        EXPECT_EQ(next.storage()[0], 'a');
        EXPECT_TRUE(current.receive_stop());
        received = true;
      }));
      failed = true;
    }));
  token.reset();
  send_byte('a'); until([&] { return failed && received; });
  EXPECT_TRUE(weak.expired());
}

TEST_F(RawUdpIO, emptyDatagramAndPartialPacketKeepAddressAndStorage) {
  setup();
  int empty = 0, partial = 0;
  ASSERT_TRUE(receiver.receive_start([&](udp&, std::size_t) { return uv::buffer_view{buffer.data(), 1}; },
    [&](udp &self, uv::udp_receive_result result) {
      ASSERT_TRUE(result);
      if (result.empty_event()) return;
      EXPECT_NE(result.address(), nullptr);
      EXPECT_EQ(result.storage().data(), buffer.data());
      if (result.count() == 0) { ++empty; EXPECT_FALSE(result.partial()); }
      else { ++partial; EXPECT_EQ(result.count(), 1); EXPECT_TRUE(result.partial()); EXPECT_EQ(result.storage()[0], 'a'); }
      if (empty == 1 && partial == 1) { EXPECT_TRUE(self.receive_stop()); }
    }));
  EXPECT_TRUE(sender.send_now(uv::buffer_view{}, destination()).ok());
  char payload[] = "abcd";
  EXPECT_TRUE(sender.send_now(uv::buffer_view{payload, 4}, destination()).ok());
  until([&] { return empty == 1 && partial == 1; });
}

struct throwing_receive {
  throwing_receive() = default;
  throwing_receive(const throwing_receive&) { throw std::runtime_error{"prepare receive"}; }
  void operator()(udp&, uv::udp_receive_result) const {}
};
TEST_F(RawUdpIO, callbackPreparationFailureKeepsCurrentSubscription) {
  setup();
  int packets = 0;
  ASSERT_TRUE(receiver.receive_start(allocate(), [&](udp &self, uv::udp_receive_result result) {
    if (!result.empty_event()) { ++packets; EXPECT_TRUE(self.receive_stop()); }
  }));
  throwing_receive rejected;
  EXPECT_THROW(receiver.receive_start(allocate(), rejected), std::runtime_error);
  send_byte('a'); until([&] { return packets == 1; });
}

TEST_F(RawUdpIO, sendBusyAndReentrantReusePreserveCallbackAndNativeData) {
  setup();
  completion state;
  operation.user_data(state);
  char payload = 'a';
  const uv::buffer_view view{&payload, 1};
  int runtime_calls = 0;
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(sender.send(operation, view, destination(), [&, token](request &current, uv::status status) {
    ++runtime_calls;
    EXPECT_FALSE(current.pending());
    EXPECT_TRUE(status);
    EXPECT_TRUE(sender.send_static<record_send>(current, std::as_bytes(std::span{&payload, 1}), destination()));
    EXPECT_TRUE(current.pending());
  }));
  token.reset();
  auto *native = operation.native();
  std::array<std::byte, sizeof(uv_udp_send_t)> before{};
  std::memcpy(before.data(), native, before.size());
  EXPECT_TRUE(operation.pending());
  EXPECT_FALSE(weak.expired());
  EXPECT_EQ(sender.send(operation, std::span<const uv::buffer_view>{}, record_send).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(sender.send_static<record_send>(operation, view, destination()).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(operation.native(), native);
  EXPECT_EQ(std::memcmp(before.data(), native, before.size()), 0);
  EXPECT_EQ(operation.user_data<completion>(), &state);
  EXPECT_TRUE(sender.send_now(view, destination()).would_block());
  until([&] { return state.calls == 1; });
  EXPECT_EQ(runtime_calls, 1);
  EXPECT_TRUE(state.outcome);
  EXPECT_TRUE(weak.expired());
}

TEST_F(RawUdpIO, nativeDestinationRejectionReleasesCaptureAndRequestCanBeReused) {
  setup();
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  char data = 'a';
  EXPECT_EQ(sender.send(operation, uv::buffer_view{&data, 1},
    [token](request&, uv::status) { ADD_FAILURE(); }).error(), uv::make_error_code(UV_EDESTADDRREQ));
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(operation.pending());
  completion state;
  operation.user_data(state);
  ASSERT_TRUE(sender.send_static<record_send>(operation, uv::buffer_view{&data, 1}, destination()));
  until([&] { return state.calls == 1; });
  EXPECT_TRUE(state.outcome);
}

TEST_F(RawUdpIO, oversizedDatagramReportsNativeErrorAtActualSubmissionOrCompletionBoundary) {
  setup();
  completion state;
  operation.user_data(state);
  std::vector<char> bytes(65536);
  const auto submitted = sender.send_static<record_send>(operation, uv::buffer_view{bytes.data(), bytes.size()}, destination());
  if (submitted) {
    until([&] { return state.calls == 1; });
    EXPECT_EQ(state.outcome.error(), uv::make_error_code(UV_EMSGSIZE));
  } else {
    EXPECT_EQ(submitted.error(), uv::make_error_code(UV_EMSGSIZE));
    EXPECT_EQ(state.calls, 0);
  }
  EXPECT_FALSE(operation.pending());
}

TEST_F(RawUdpIO, closeKeepsRequestAndPayloadOwnerUntilCompletion) {
  setup();
  auto owned = std::make_shared<std::array<char, 1>>(std::array{'a'});
  std::weak_ptr<std::array<char, 1>> weak = owned;
  bool completed = false, closed = false;
  ASSERT_TRUE(sender.send(operation, uv::buffer_view{owned->data(), owned->size()}, destination(),
    [&, owned](request &current, uv::status status) {
      EXPECT_FALSE(current.pending());
      EXPECT_FALSE(weak.expired());
      EXPECT_TRUE(status || status.error() == uv::make_error_code(UV_ECANCELED));
      completed = true;
    }));
  owned.reset();
  sender.close([&](udp&) { EXPECT_TRUE(completed); EXPECT_TRUE(weak.expired()); closed = true; });
  EXPECT_TRUE(operation.pending());
  EXPECT_FALSE(weak.expired());
  until([&] { return completed && closed; });
}

TEST_F(RawUdpIO, realReceiveCloseKeepsAllocatorOwnedStorageDuringDelivery) {
  setup();
  auto owned = std::make_shared<std::array<char, 16>>();
  std::weak_ptr<std::array<char, 16>> weak = owned;
  bool closed = false;
  ASSERT_TRUE(receiver.receive_start([owned](udp&, std::size_t) { return uv::buffer_view{owned->data(), owned->size()}; },
    [&](udp &self, uv::udp_receive_result result) {
      ASSERT_TRUE(result);
      if (result.empty_event()) return;
      self.close([&](udp&) { EXPECT_TRUE(weak.expired()); closed = true; });
      ASSERT_FALSE(weak.expired());
      EXPECT_EQ(result.storage()[0], 'a');
    }));
  owned.reset(); send_byte('a'); until([&] { return closed; });
}

#if UVPP_HAS_UDP_CONNECT
TEST_F(RawUdpIO, connectedSendUsesNoDestinationAndPreservesEmptyPayload) {
  setup();
  int packets = 0;
  ASSERT_TRUE(receiver.receive_start(allocate(), [&](udp &self, uv::udp_receive_result result) {
    if (result.empty_event()) return;
    ++packets;
    EXPECT_EQ(result.count(), 0);
    EXPECT_TRUE(self.receive_stop());
  }));
  ASSERT_TRUE(sender.connect(destination()));
  completion state;
  operation.user_data(state);
  ASSERT_TRUE(sender.send(operation, std::span<const std::byte>{}, record_send));
  until([&] { return packets == 1 && state.calls == 1; });
  EXPECT_TRUE(state.outcome);
}
#endif

#if UVPP_HAS_UDP_INIT_EX && UVPP_HAS_UDP_RECVMMSG_FLAG && UVPP_HAS_UDP_USING_RECVMMSG && UVPP_HAS_UDP_MMSG_FREE
class RawUdpMmsg : public RawUdpIO, public ::testing::WithParamInterface<int> {};
TEST_P(RawUdpMmsg, realBatchKeepsOriginalPairForNativeDeliveriesWithoutForcingFree) {
  setup(true);
  if (!receiver.using_recvmmsg()) { GTEST_SKIP() << "Native recvmmsg unavailable"; }
  receiver.user_data(static_state);
  static_state.wait_for_marker = true;
  // Compare stop's native callback state with a direct libuv stop. In particular,
  // uvpp must not restore a callback cleared by the current native backend.
  udp reference{loop, uv::udp_socket_family::ipv4, uv::udp_init_flag::recvmmsg};
  ASSERT_EQ(uv_udp_recv_start(reference.native(),
    [](uv_handle_t*, std::size_t, uv_buf_t *out) { *out = uv_buf_init(nullptr, 0); },
    [](uv_udp_t*, ssize_t, const uv_buf_t*, const sockaddr*, unsigned) {}), 0);
  ASSERT_EQ(uv_udp_recv_stop(reference.native()), 0);
  const bool native_receive_cleared = reference.native()->recv_cb == nullptr;
  const bool native_allocator_cleared = reference.native()->alloc_cb == nullptr;
  reference.close();
  auto owned = std::make_shared<std::vector<char>>(4 * 65536);
  const auto *base = owned->data();
  std::weak_ptr<std::vector<char>> weak = owned;
  int chunks = 0, frees = 0;
  bool closed = false;
  ASSERT_TRUE(receiver.receive_start([owned](udp&, std::size_t) { return uv::buffer_view{owned->data(), owned->size()}; },
    [&](udp &self, uv::udp_receive_result result) {
      ASSERT_TRUE(result);
      ASSERT_FALSE(weak.expired());
      if (result.mmsg_free()) {
        ++frees;
        EXPECT_EQ(result.storage().data(), base);
        if (GetParam() == 3) { EXPECT_TRUE(self.receive_stop()); }
        return;
      }
      if (result.empty_event()) return;
      EXPECT_TRUE(result.mmsg_chunk());
      EXPECT_EQ(result.count(), 1);
      ++chunks;
      if (chunks == 1) {
        if (GetParam() == 0) {
          EXPECT_TRUE(self.receive_stop());
          EXPECT_FALSE(self.active());
          EXPECT_EQ(self.native()->recv_cb == nullptr, native_receive_cleared);
          EXPECT_EQ(self.native()->alloc_cb == nullptr, native_allocator_cleared);
        }
        if (GetParam() == 1) { EXPECT_TRUE((self.receive_start_static<static_allocate, static_receive>())); }
        if (GetParam() == 2) { self.close([&](udp&) { EXPECT_TRUE(weak.expired()); closed = true; }); }
      }
      ASSERT_FALSE(weak.expired());
      EXPECT_EQ(result.storage()[0], 'a' + chunks - 1);
    }));
  owned.reset();
  send_byte('a'); send_byte('b'); send_byte('c');
  until([&] { return chunks >= 1 && (GetParam() != 3 || frees == 1); });
  if (GetParam() == 3) {
    EXPECT_EQ(frees, 1);
    EXPECT_TRUE(weak.expired());
  }
  EXPECT_GE(chunks, 1);
  EXPECT_LE(chunks, 3);
  if (GetParam() == 1) {
    send_byte('d'); until([&] { return static_state.marker_received; });
  }
  // Stop/close may suppress native FREE or remaining chunks. Keep a pending
  // allocator loan until the next allocation or close completion, without
  // synthesizing notifications or changing native callback fields.
  if (GetParam() != 2) {
    receiver.close([&](udp&) { EXPECT_TRUE(weak.expired()); closed = true; });
  }
  until([&] { return closed; });
  EXPECT_TRUE(weak.expired());
  EXPECT_LE(frees, 1);
}
INSTANTIATE_TEST_SUITE_P(StopReplaceCloseOrContinue, RawUdpMmsg, ::testing::Values(0, 1, 2, 3));
#endif

#if UVPP_HAS_UDP_TRY_SEND2
TEST_F(RawUdpIO, batchValidationRejectsInvalidMetadataBeforeSendingAnyPacket) {
  setup();
  EXPECT_EQ(sender.send_many_now(1, nullptr, nullptr, nullptr).error_code(), uv::make_error_code(UV_EINVAL));
  uv::udp_send_batch empty;
  EXPECT_EQ(sender.send_many_now(empty).error_code(), uv::make_error_code(UV_EINVAL));
  std::array<uv_buf_t*, 2> buffers{};
  std::array<unsigned int, 1> counts{};
  std::array<sockaddr*, 2> addresses{};
  EXPECT_THROW((uv::udp_send_many_view{buffers, counts, addresses}), std::invalid_argument);
  char byte = 'a';
  auto destination_address = destination();
  uv::udp_send_batch batch;
  batch.add(uv::buffer_view{&byte, 1}, destination_address).add(std::span<const uv::buffer_view>{}, destination_address);
  EXPECT_EQ(sender.send_many_now(batch).error_code(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(sender.send_many_now(batch, batch.size()).error_code(), uv::make_error_code(UV_EINVAL));
}

TEST_F(RawUdpIO, partialBatchReportsSentPrefixAndRetryDoesNotResendIt) {
  setup();
  int packets = 0;
  ASSERT_TRUE(receiver.receive_start(allocate(), [&](udp &self, uv::udp_receive_result result) {
    if (result.empty_event()) return;
    EXPECT_EQ(result.count(), 1);
    EXPECT_EQ(result.storage()[0], 'a');
    ++packets;
    EXPECT_TRUE(self.receive_stop());
  }));
  char byte = 'a';
  std::vector<char> oversized(65536);
  auto target = destination();
  uv::udp_send_batch batch;
  batch.add(uv::buffer_view{&byte, 1}, target).add(uv::buffer_view{oversized.data(), oversized.size()}, target);
  const auto first = sender.send_many_now(batch);
  // Some backends reject the entire syscall rather than report a sent prefix.
  if (!first.ok()) {
    EXPECT_EQ(first.error_code(), uv::make_error_code(UV_EMSGSIZE));
    EXPECT_TRUE(receiver.receive_stop());
    return;
  }
  EXPECT_EQ(first.datagrams_sent(), 1u);
  const auto retry = sender.send_many_now(batch, first.datagrams_sent());
  EXPECT_EQ(retry.error_code(), uv::make_error_code(UV_EMSGSIZE));
  until([&] { return packets == 1; });
}

TEST_F(RawUdpIO, pendingNativeSendReportsWouldBlockForBatchWithoutLosingProgress) {
  setup();
  completion state;
  operation.user_data(state);
  char data = 'a';
  auto target = destination();
  ASSERT_TRUE(sender.send(operation, uv::buffer_view{&data, 1}, target, record_send));
  uv::udp_send_batch batch;
  batch.add(uv::buffer_view{&data, 1}, target);
  EXPECT_TRUE(sender.send_many_now(batch).would_block());
  until([&] { return state.calls == 1; });
  const auto sent = sender.send_many_now(batch);
  EXPECT_TRUE(sent.ok());
  EXPECT_EQ(sent.datagrams_sent(), 1u);
}
#endif

TEST_F(RawUdpIO, runtimeCallbackExceptionsTerminateAtNativeBoundary) {
  setup();
  EXPECT_DEATH({
    sender.send(operation, uv::buffer_view{buffer.data(), 1}, destination(), [](request&, uv::status) { throw 1; }).throw_if_error();
    loop.run();
  }, "");
  ASSERT_TRUE(receiver.receive_start([](udp&, std::size_t) -> uv::buffer_view { throw 1; }, [](udp&, uv::udp_receive_result) {}));
  send_byte('a');
  EXPECT_DEATH(loop.run(), "");
  EXPECT_TRUE(receiver.receive_stop());
}
}
