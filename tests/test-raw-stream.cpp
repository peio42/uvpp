#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/handles/tcp.hpp"
#include "uvpp/handles/pipe.hpp"

namespace {
using tcp = uv::raw::tcp;

struct static_state {
  std::array<char, 32> buffer{};
  int reads = 0;
  int connections = 0;
  int closes = 0;
};
uv::buffer_view static_allocate(tcp &self, std::size_t) {
  auto &buffer = self.user_data<static_state>()->buffer;
  return uv::buffer_view{buffer.data(), buffer.size()};
}
void static_read(tcp &self, uv::read_result) { ++self.user_data<static_state>()->reads; }
void static_connection(tcp &self, uv::status) { ++self.user_data<static_state>()->connections; }
void static_close(tcp &self) { ++self.user_data<static_state>()->closes; }

class RawStream : public ::testing::Test {
protected:
  uv::loop loop;
  tcp server{loop};
  tcp client{loop};
  tcp peer{loop};
  uv::raw::connect_request connect;
  std::array<char, 32> buffer{};

  ~RawStream() override {
    for (auto *handle : {&server, &client, &peer}) handle->close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }
  template<class Predicate>
  void until(Predicate done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!done() && std::chrono::steady_clock::now() < end) {
      loop.run(uv::run_mode::nowait);
      std::this_thread::yield();
    }
    ASSERT_TRUE(done());
  }
  void pair() {
    bool accepted = false, connected = false;
    server.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
    server.listen([&](tcp &self, uv::status status) {
      EXPECT_TRUE(status);
      EXPECT_TRUE(self.accept(peer));
      accepted = true;
    }).throw_if_error();
    client.connect(connect, server.sockname().value().to_v4(), [&](uv::raw::connect_request&, uv::status status) {
      EXPECT_TRUE(status);
      connected = true;
    }).throw_if_error();
    until([&] { return accepted && connected; });
    // The connection is established; no further event should use these locals.
    server.close();
  }
  auto allocator() {
    return [this](tcp&, std::size_t) { return uv::buffer_view{buffer.data(), buffer.size()}; };
  }
  void deliver(ssize_t count = 0) {
    auto *native = client.native_stream();
    const auto reader = native->read_cb;
    uv_buf_t buf{};
    native->alloc_cb(client.native_handle(), buffer.size(), &buf);
    reader(native, count, &buf);
  }
};

TEST_F(RawStream, guardedOperationsAndTypedReadObservations) {
  tcp empty;
  const auto badf = uv::make_error_code(UV_EBADF);
  static_assert(std::is_same_v<decltype(empty.listen({})), uv::status>);
  static_assert(std::is_same_v<decltype(empty.accept(client)), uv::status>);
  static_assert(std::is_same_v<decltype(empty.read_stop()), uv::status>);
  static_assert(std::is_base_of_v<uv::raw::stream<tcp, uv_tcp_t>, tcp>);
  EXPECT_EQ(empty.listen({}).error(), badf);
  EXPECT_EQ(empty.read_start({}, {}).error(), badf);
  EXPECT_EQ(empty.read_stop().error(), badf);
  EXPECT_EQ(empty.accept(client).error(), badf);
  EXPECT_EQ(empty.blocking(true).error(), badf);
  EXPECT_FALSE(empty.readable());
  EXPECT_FALSE(empty.writable());
  EXPECT_EQ(empty.write_queue_size(), 0u);
  empty.close();
  auto native = uv_buf_init(buffer.data(), static_cast<unsigned int>(buffer.size()));
  uv::read_result zero{0, &native};
  EXPECT_TRUE(zero);
  EXPECT_FALSE(zero.eof());
  EXPECT_TRUE(zero.bytes().empty());
  EXPECT_EQ(zero.storage().data(), buffer.data());
  uv::read_result large{(std::numeric_limits<ssize_t>::max)(), nullptr};
  EXPECT_TRUE(large.status());
  uv::read_result eof{UV_EOF, nullptr};
  EXPECT_TRUE(eof.eof());
  EXPECT_EQ(eof.status().error(), uv::make_error_code(UV_EOF));
  client.close();
  EXPECT_EQ(client.read_stop().error(), badf);
  EXPECT_EQ(client.listen_static<static_connection>().error(), badf);
  EXPECT_EQ((client.read_start_static<static_allocate, static_read>().error()), badf);
}

TEST_F(RawStream, blockingModeRetainsBackendOutcome) {
  pair();
#ifdef _WIN32
  EXPECT_EQ(client.blocking(true).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.blocking(false).error(), uv::make_error_code(UV_EINVAL));
#else
  EXPECT_TRUE(client.blocking(true));
  EXPECT_TRUE(client.blocking(false));
#endif
}

TEST_F(RawStream, acceptValidatesClientsAndPreservesNativeWouldBlock) {
  server.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
  ASSERT_TRUE(server.listen_static<static_connection>());
  EXPECT_EQ(server.accept(client).error(), uv::make_error_code(UV_EAGAIN));
  EXPECT_EQ(server.accept(server).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.accept(peer).error(), uv::make_error_code(UV_EINVAL));
  tcp empty;
  EXPECT_EQ(server.accept(empty).error(), uv::make_error_code(UV_EBADF));
  uv::loop other_loop;
  tcp foreign{other_loop};
  EXPECT_EQ(server.accept(foreign).error(), uv::make_error_code(UV_EINVAL));
  uv::raw::pipe wrong_type{loop};
  EXPECT_EQ(server.accept(wrong_type).error(), uv::make_error_code(UV_EINVAL));
  bool pipe_closed = false;
  wrong_type.close([&](uv::raw::pipe&) { pipe_closed = true; });
  until([&] { return pipe_closed; });
  foreign.close();
  other_loop.run();
  EXPECT_NO_THROW(other_loop.close());
}

TEST_F(RawStream, listenerReplacementCommitsAfterSuccessAndPreservesExecutingCallableOnFailure) {
  server.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
  auto token = std::make_shared<int>(7);
  std::weak_ptr<int> weak = token;
  int calls = 0;
  ASSERT_TRUE(server.listen([&, token, count = 0](tcp &self, uv::status status) mutable {
    calls = ++count;
    EXPECT_EQ(status.error(), uv::make_error_code(UV_ECONNABORTED));
    auto rejected = std::make_shared<int>(8);
    std::weak_ptr<int> rejected_weak = rejected;
    EXPECT_EQ(self.listen(0, [rejected](tcp&, uv::status) {}).error(), uv::make_error_code(UV_EINVAL));
    rejected.reset();
    EXPECT_TRUE(rejected_weak.expired());
  }));
  token.reset();
#ifdef _WIN32
  auto dispatch = server.native_stream()->stream.serv.connection_cb;
#else
  auto dispatch = server.native_stream()->connection_cb;
#endif
  dispatch(server.native_stream(), UV_ECONNABORTED);
  dispatch(server.native_stream(), UV_ECONNABORTED);
  EXPECT_EQ(calls, 2);
  EXPECT_FALSE(weak.expired());
  static_state state;
  server.user_data(state);
  ASSERT_TRUE(server.listen_static<static_connection>());
  EXPECT_TRUE(weak.expired());
  dispatch(server.native_stream(), 0);
  EXPECT_EQ(state.connections, 1);
  ASSERT_TRUE(server.listen([&](tcp &self, uv::status) { self.close(); }));
  dispatch(server.native_stream(), 0);
  EXPECT_TRUE(server.closing());
}

TEST_F(RawStream, nativeInactiveListenerErrorReleasesSelectionBeforeDelivery) {
  server.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  int calls = 0;
  ASSERT_TRUE(server.listen([&, token](tcp &self, uv::status result) {
    ++calls;
    EXPECT_EQ(result.error(), uv::make_error_code(UV_EIO));
    EXPECT_FALSE(self.active());
  }));
  token.reset();
#ifdef _WIN32
  auto dispatch = server.native_stream()->stream.serv.connection_cb;
#else
  auto dispatch = server.native_stream()->connection_cb;
#endif
  // Model the native boundary after a backend has quiesced the listener. Using
  // native close here avoids depending on private activity-flag bit values.
  uv_close(server.native_handle(), nullptr);
  dispatch(server.native_stream(), UV_EIO);
  EXPECT_TRUE(weak.expired());
  dispatch(server.native_stream(), UV_EIO);
  EXPECT_EQ(calls, 1);
}

TEST_F(RawStream, failedNativeListenReleasesRejectedCallback) {
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  // A connected stream cannot become a listener.
  pair();
  EXPECT_FALSE(client.listen([token](tcp&, uv::status) {}));
  token.reset();
  EXPECT_TRUE(weak.expired());
}

TEST_F(RawStream, readReplacementFromCallbackRetainsMutableStateAndReleasesRetiredPair) {
  pair();
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  int first = 0, second = 0;
  ASSERT_TRUE(client.read_start(allocator(), [&, token](tcp &self, uv::read_result read) {
    ++first;
    EXPECT_EQ(read.count(), 0);
    EXPECT_TRUE(self.read_start(allocator(), [&, count = 0](tcp&, uv::read_result) mutable { second = ++count; }));
    EXPECT_FALSE(weak.expired());
  }));
  token.reset();
  deliver();
  EXPECT_TRUE(weak.expired());
  deliver();
  deliver();
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 2);
  EXPECT_TRUE(client.read_stop());
  EXPECT_FALSE(client.active());
}

class RawStreamNativeLoan : public RawStream, public ::testing::WithParamInterface<bool> {};
TEST_P(RawStreamNativeLoan, reconfigurationKeepsDeliveredBufferAliveUntilReaderReturns) {
  pair();
  const bool replace = GetParam();
  auto owned = std::make_shared<std::array<char, 8>>();
  std::weak_ptr<std::array<char, 8>> weak = owned;
  const auto *loan_address = owned->data();
  int allocations = 0, old_deliveries = 0, new_deliveries = 0;
  bool reconfigured = false, received_second = false;

  const auto next_reader = [&](tcp &self, uv::read_result read) {
    ++new_deliveries;
    ASSERT_TRUE(read);
    if (read.count() == 0) return;
    EXPECT_EQ(read.count(), 1);
    EXPECT_EQ(read.storage().data(), buffer.data());
    EXPECT_EQ(read.storage()[0], 'b');
    EXPECT_TRUE(weak.expired());
    EXPECT_TRUE(self.read_stop());
    received_second = true;
  };
  ASSERT_TRUE(client.read_start(
    [owned, &allocations](tcp&, std::size_t) {
      ++allocations;
      return uv::buffer_view{owned->data(), owned->size()};
    },
    [&](tcp &self, uv::read_result read) {
      ++old_deliveries;
      ASSERT_TRUE(read);
      if (read.count() == 0) return;
      EXPECT_EQ(read.count(), 1);
      EXPECT_EQ(read.storage().data(), loan_address);
      if (replace) EXPECT_TRUE(self.read_start(allocator(), next_reader));
      else EXPECT_TRUE(self.read_stop());

      // Neither this reader nor the test owns the buffer. Retiring the active
      // subscription must keep its allocator's capture alive through delivery.
      ASSERT_FALSE(weak.expired());
      EXPECT_EQ(read.storage()[0], 'a');
      EXPECT_EQ(read.bytes().size(), 1u);
      EXPECT_EQ(self.active(), replace);
      reconfigured = true;
    }));
  owned.reset();

  // Real socket I/O and libuv dispatch only: no direct callback invocation.
  char first = 'a';
  const auto first_write = peer.write_now(uv::buffer_view{&first, 1});
  ASSERT_TRUE(first_write.ok());
  ASSERT_EQ(first_write.bytes_written(), 1u);
  until([&] { return reconfigured; });
  ASSERT_TRUE(reconfigured);
  EXPECT_GT(allocations, 0);
  EXPECT_TRUE(weak.expired());
  const auto retired_deliveries = old_deliveries;

  char second = 'b';
  const auto second_write = peer.write_now(uv::buffer_view{&second, 1});
  ASSERT_TRUE(second_write.ok());
  ASSERT_EQ(second_write.bytes_written(), 1u);
  if (!replace) {
    loop.run(uv::run_mode::nowait);
    EXPECT_FALSE(client.active());
    EXPECT_EQ(old_deliveries, retired_deliveries);
    EXPECT_EQ(new_deliveries, 0);
    ASSERT_TRUE(client.read_start(allocator(), next_reader));
  }
  until([&] { return received_second; });
  EXPECT_TRUE(received_second);
  EXPECT_EQ(old_deliveries, retired_deliveries);
  EXPECT_GE(new_deliveries, 1);
  EXPECT_FALSE(client.active());
}
INSTANTIATE_TEST_SUITE_P(RealTcp, RawStreamNativeLoan, ::testing::Values(false, true),
  [](const ::testing::TestParamInfo<bool> &info) { return info.param ? "Replace" : "Stop"; });

TEST_F(RawStream, rejectedReadReplacementPreservesCurrentSubscription) {
  pair();
  int calls = 0;
  ASSERT_TRUE(client.read_start(allocator(), [&, count = 0](tcp &self, uv::read_result) mutable {
    calls = ++count;
    EXPECT_EQ(self.read_start({}, [](tcp&, uv::read_result) {}).error(), uv::make_error_code(UV_EINVAL));
    EXPECT_EQ(self.read_start(allocator(), {}).error(), uv::make_error_code(UV_EINVAL));
  }));
  deliver();
  deliver();
  EXPECT_EQ(calls, 2);
  EXPECT_TRUE(client.active());
}

TEST_F(RawStream, stopFromReaderDoesNotRestoreExecutingCallbacks) {
  pair();
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(client.read_start(allocator(), [&, token](tcp &self, uv::read_result) {
    EXPECT_TRUE(self.read_stop());
    EXPECT_FALSE(self.active());
    EXPECT_FALSE(weak.expired());
  }));
  token.reset();
  deliver();
  EXPECT_TRUE(weak.expired());
  EXPECT_TRUE(client.read_stop());
}

TEST_F(RawStream, outstandingBufferUsesOriginalReaderAcrossReplacement) {
  pair();
  auto owned = std::make_shared<std::array<char, 8>>();
  std::weak_ptr<std::array<char, 8>> weak = owned;
  int old_reads = 0, new_reads = 0;
  ASSERT_TRUE(client.read_start([owned](tcp&, std::size_t) { return uv::buffer_view{owned->data(), owned->size()}; },
    [&](tcp&, uv::read_result read) { ++old_reads; EXPECT_EQ(read.storage().data(), weak.lock()->data()); }));
  owned.reset();
  auto *native = client.native_stream();
  uv_buf_t buf{};
  native->alloc_cb(client.native_handle(), 8, &buf);
  ASSERT_TRUE(client.read_start(allocator(), [&](tcp&, uv::read_result) { ++new_reads; }));
  EXPECT_FALSE(weak.expired());
  native->read_cb(native, 1, &buf);
  EXPECT_TRUE(weak.expired());
  deliver();
  EXPECT_EQ(old_reads, 1);
  EXPECT_EQ(new_reads, 1);
}

TEST_F(RawStream, canceledBufferLoanRetiresAtNextAllocationWithoutLosingCurrentSubscription) {
  pair();
  int calls = 0;
  ASSERT_TRUE(client.read_start(allocator(), [&](tcp&, uv::read_result) { ++calls; }));
  uv_buf_t buf{};
  auto *native = client.native_stream();
  native->alloc_cb(client.native_handle(), buffer.size(), &buf);
  // A backend may finish with a loan without producing a read notification.
  native->alloc_cb(client.native_handle(), buffer.size(), &buf);
  native->read_cb(native, 0, &buf);
  EXPECT_EQ(calls, 1);
}

TEST_F(RawStream, terminalReadStopsAndReleasesSubscriptionBeforeDelivery) {
  pair();
  auto allocated = std::make_shared<int>(1), read = std::make_shared<int>(2);
  std::weak_ptr<int> alloc_weak = allocated, read_weak = read;
  bool delivered = false;
  ASSERT_TRUE(client.read_start([&, allocated](tcp&, std::size_t) { return uv::buffer_view{buffer.data(), buffer.size()}; },
    [&, read](tcp &self, uv::read_result result) {
      EXPECT_EQ(result.count(), UV_ENOBUFS);
      EXPECT_FALSE(self.active());
      EXPECT_FALSE(alloc_weak.expired()); // The executing pair still owns its loan.
      EXPECT_FALSE(read_weak.expired());
      delivered = true;
      EXPECT_TRUE((self.read_start_static<static_allocate, static_read>()));
    }));
  allocated.reset(); read.reset();
  static_state state;
  client.user_data(state);
  deliver(UV_ENOBUFS);
  EXPECT_TRUE(delivered);
  EXPECT_TRUE(alloc_weak.expired());
  EXPECT_TRUE(read_weak.expired());
  deliver();
  EXPECT_EQ(state.reads, 1);
}

TEST_F(RawStream, realReadAndEofKeepBorrowedPayloadAndQuiesceBeforeDelivery) {
  pair();
  bool got_data = false, eof = false;
  ASSERT_TRUE(client.read_start(allocator(), [&](tcp &self, uv::read_result read) {
    if (read.count() > 0) {
      EXPECT_EQ(read.bytes().size(), 3u);
      EXPECT_EQ(read.storage().data(), buffer.data());
      EXPECT_TRUE(read.status());
      got_data = true;
    } else if (read.eof()) {
      eof = true;
      EXPECT_FALSE(self.active());
      self.close();
    } else EXPECT_TRUE(read);
  }));
  uv::raw::write_request write;
  char payload[] = "abc";
  bool written = false, shut = false;
  peer.write(write, uv::buffer_view{payload, 3}, [&](uv::raw::write_request&, uv::status status) { EXPECT_TRUE(status); written = true; }).throw_if_error();
  uv::raw::shutdown_request shutdown;
  peer.shutdown(shutdown, [&](uv::raw::shutdown_request&, uv::status status) { EXPECT_TRUE(status); shut = true; }).throw_if_error();
  until([&] { return got_data && eof && written && shut; });
}

TEST_F(RawStream, closeRetainsOutstandingLoanUntilCompletion) {
  pair();
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(client.read_start([&, token](tcp&, std::size_t) { return uv::buffer_view{buffer.data(), buffer.size()}; },
    [](tcp&, uv::read_result) {}));
  token.reset();
  uv_buf_t buf{};
  client.native_stream()->alloc_cb(client.native_handle(), buffer.size(), &buf);
  bool closed = false;
  client.close([&](tcp&) { EXPECT_TRUE(weak.expired()); closed = true; });
  EXPECT_FALSE(weak.expired());
  until([&] { return closed; });
  EXPECT_TRUE(weak.expired());
}

TEST_F(RawStream, failedNativeReadStartDropsBothRejectedCallbacks) {
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  EXPECT_FALSE(client.read_start([&, token](tcp&, std::size_t) { return uv::buffer_view{buffer.data(), buffer.size()}; },
                                [token](tcp&, uv::read_result) {}));
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(client.active());
}

TEST_F(RawStream, failedReentrantStartAfterCloseDoesNotRestoreStoppedPair) {
  pair();
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(client.read_start(allocator(), [&, token](tcp &self, uv::read_result) {
    EXPECT_TRUE(self.read_stop());
    self.close();
    auto rejected = std::make_shared<int>(1);
    std::weak_ptr<int> rejected_weak = rejected;
    EXPECT_EQ(self.read_start(allocator(), [rejected](tcp&, uv::read_result) {}).error(), uv::make_error_code(UV_EBADF));
    rejected.reset();
    EXPECT_TRUE(rejected_weak.expired());
    EXPECT_FALSE(weak.expired());
  }));
  token.reset();
  deliver();
  EXPECT_TRUE(weak.expired());
}

struct throwing_reader {
  throwing_reader() = default;
  throwing_reader(const throwing_reader&) { throw std::runtime_error{"prepare reader"}; }
  void operator()(tcp&, uv::read_result) const {}
};
TEST_F(RawStream, callbackPreparationFailureDoesNotChangeNativeSubscription) {
  pair();
  int calls = 0;
  ASSERT_TRUE(client.read_start(allocator(), [&](tcp&, uv::read_result) { ++calls; }));
  throwing_reader rejected;
  EXPECT_THROW(client.read_start(allocator(), rejected), std::runtime_error);
  deliver();
  EXPECT_EQ(calls, 1);
}

TEST_F(RawStream, staticAndRuntimeReadsReplaceEachOtherAndStaticCloseReleasesCaptures) {
  pair();
  static_state state;
  client.user_data(state);
  ASSERT_TRUE((client.read_start_static<static_allocate, static_read>()));
  deliver();
  EXPECT_EQ(state.reads, 1);
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(client.read_start(allocator(), [token](tcp &self, uv::read_result) { self.close_static<static_close>(); }));
  token.reset();
  deliver();
  EXPECT_TRUE(weak.expired());
  until([&] { return state.closes == 1; });
}
class RawStreamClose : public RawStream, public ::testing::WithParamInterface<int> {};
TEST_P(RawStreamClose, outstandingLoanSurvivesUntilNativeCloseCompletion) {
  pair();
  static_state state;
  client.user_data(state);
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(client.read_start([&, token](tcp&, std::size_t) { return uv::buffer_view{buffer.data(), buffer.size()}; },
                                [token](tcp&, uv::read_result) {}));
  token.reset();
  uv_buf_t buf{};
  client.native_stream()->alloc_cb(client.native_handle(), buffer.size(), &buf);
  const auto closed = [&](tcp&) { EXPECT_TRUE(weak.expired()); ++state.closes; };
  switch (GetParam()) {
    case 0: client.close(); break;
    case 1: client.close(closed); break;
    case 2: client.close_static<static_close>(); break;
#if UVPP_HAS_TCP_CLOSE_RESET
    case 3: ASSERT_TRUE(client.close_reset()); break;
    case 4: ASSERT_TRUE(client.close_reset(closed)); break;
    case 5: ASSERT_TRUE(client.close_reset_static<static_close>()); break;
#endif
  }
  EXPECT_FALSE(weak.expired());
  until([&] { return weak.expired(); });
  EXPECT_EQ(state.closes, GetParam() % 3 == 0 ? 0 : 1);
}
INSTANTIATE_TEST_SUITE_P(NativeCloseForms, RawStreamClose, ::testing::Values(0, 1, 2
#if UVPP_HAS_TCP_CLOSE_RESET
  , 3, 4, 5
#endif
));

TEST_F(RawStream, callbackExceptionsTerminateAtNativeBoundary) {
  pair();
  ASSERT_TRUE(client.read_start([](tcp&, std::size_t) -> uv::buffer_view { throw std::runtime_error{"allocator"}; },
                                [](tcp&, uv::read_result) {}));
  EXPECT_DEATH({
    uv_buf_t buf{};
    client.native_stream()->alloc_cb(client.native_handle(), 8, &buf);
  }, "");
  ASSERT_TRUE(client.read_start(allocator(), [](tcp&, uv::read_result) { throw std::runtime_error{"reader"}; }));
  EXPECT_DEATH(deliver(), "");
}

}
