#include <array>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/handles/tcp.hpp"

namespace {
using uv::raw::tcp;
using uv::raw::connect_request;
using uv::raw::write_request;
using uv::raw::shutdown_request;

struct probe {
  int calls = 0;
  uv::status outcome;
};
template<class Request>
void observe(Request &request, uv::status status) {
  EXPECT_FALSE(request.pending());
  auto &state = *request.template user_data<probe>();
  ++state.calls;
  state.outcome = status;
}

class RawStreamRequests : public ::testing::Test {
protected:
  uv::loop loop;
  tcp server{loop}, client{loop}, peer{loop}, client2{loop}, peer2{loop};
  connect_request connecting;
  write_request writing;
  shutdown_request shutting;
  int accepted = 0;
  bool connected = false;
  std::array<char, 32> storage{};

  ~RawStreamRequests() override {
    for (auto *handle : {&server, &client, &peer, &client2, &peer2}) handle->close();
    loop.run();
    EXPECT_FALSE(connecting.pending());
    EXPECT_FALSE(writing.pending());
    EXPECT_FALSE(shutting.pending());
    EXPECT_NO_THROW(loop.close());
  }
  template<class Predicate>
  void until(Predicate done) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!done() && std::chrono::steady_clock::now() < end) {
      loop.run(uv::run_mode::nowait);
      std::this_thread::yield();
    }
    EXPECT_TRUE(done());
  }
  void listen() {
    server.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
    server.listen([this](tcp &self, uv::status status) {
      EXPECT_TRUE(status);
      EXPECT_TRUE(self.accept(accepted == 0 ? peer : peer2));
      ++accepted;
    }).throw_if_error();
  }
  uv::ipv4 address() { return server.sockname().value().to_v4(); }
  void pair() {
    listen();
    client.connect(connecting, address(), [this](connect_request &request, uv::status status) {
      EXPECT_FALSE(request.pending());
      EXPECT_TRUE(status);
      connected = true;
    }).throw_if_error();
    until([&] { return connected && accepted == 1; });
  }
};

TEST_F(RawStreamRequests, defaultRequestsAndInvalidHandlesHaveExplicitOutcomes) {
  static_assert(!std::is_copy_constructible_v<write_request>);
  static_assert(!std::is_move_constructible_v<connect_request>);
  static_assert(!std::is_move_constructible_v<shutdown_request>);
  static_assert(std::is_same_v<decltype(client.shutdown(shutting, observe<shutdown_request>)), uv::status>);
  EXPECT_FALSE(connecting.pending());
  EXPECT_FALSE(writing.pending());
  EXPECT_FALSE(shutting.pending());
  tcp empty;
  const auto addr = uv::ipv4{"127.0.0.1", 1};
  const auto badf = uv::make_error_code(UV_EBADF);
  EXPECT_EQ(empty.connect(connecting, addr, observe<connect_request>).error(), badf);
  EXPECT_EQ(empty.connect_static<observe<connect_request>>(connecting, addr).error(), badf);
  EXPECT_EQ(empty.write(writing, uv::buffer_view{}, observe<write_request>).error(), badf);
  EXPECT_EQ(empty.write_static<observe<write_request>>(writing, uv::buffer_view{}).error(), badf);
  EXPECT_EQ(empty.shutdown(shutting, observe<shutdown_request>).error(), badf);
  EXPECT_EQ(empty.shutdown_static<observe<shutdown_request>>(shutting).error(), badf);
  EXPECT_EQ(empty.write_now(uv::buffer_view{}).error_code(), badf);
  client.close();
  EXPECT_EQ(client.connect(connecting, addr, observe<connect_request>).error(), badf);
  EXPECT_EQ(client.shutdown(shutting, observe<shutdown_request>).error(), badf);
  EXPECT_EQ(client.write(writing, uv::buffer_view{}, observe<write_request>).error(), badf);
}

TEST_F(RawStreamRequests, invalidInputsDropCallbacksWithoutPendingOrNativeSubmission) {
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  EXPECT_EQ(client.write(writing, std::span<const uv::buffer_view>{}, [token](write_request&, uv::status) {}).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.write_static<observe<write_request>>(writing, std::span<const uv::buffer_view>{}).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.write(writing, uv::buffer_view{nullptr, 1}, [token](write_request&, uv::status) {}).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.write(writing, uv::buffer_view{}, {}).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.shutdown(shutting, {}).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(client.connect(connecting, uv::ipv4{"127.0.0.1", 1}, {}).error(), uv::make_error_code(UV_EINVAL));
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(writing.pending());
  EXPECT_FALSE(shutting.pending());
  EXPECT_FALSE(connecting.pending());
}

TEST_F(RawStreamRequests, nativeSubmissionFailureReleasesCallbackAndRequestCanBeReused) {
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  const uv::buffer_view payload{storage.data(), 1};
  EXPECT_FALSE(client.write(writing, payload, [token](write_request&, uv::status) {}));
  EXPECT_FALSE(client.shutdown(shutting, [token](shutdown_request&, uv::status) {}));
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(writing.pending());
  EXPECT_FALSE(shutting.pending());
  pair();
  probe write, shutdown;
  writing.user_data(write);
  shutting.user_data(shutdown);
  ASSERT_TRUE(client.write_static<observe<write_request>>(writing, payload));
  ASSERT_TRUE(client.shutdown_static<observe<shutdown_request>>(shutting));
  until([&] { return write.calls == 1 && shutdown.calls == 1; });
  EXPECT_TRUE(write.outcome);
  EXPECT_TRUE(shutdown.outcome);
}

template<class Request>
auto request_state(Request &request) {
  return std::tuple{request.native(), request.native_request(), request.native()->data,
                    request.pending(), request.type(), request.native()->handle,
                    request.native()->cb};
}

auto stream_state(tcp &handle) {
  return std::tuple{handle.native(), handle.native_handle(), handle.native_stream(),
                    handle.native()->data, handle.native()->loop, handle.initialized(),
                    handle.closing(), handle.active(), handle.has_ref(), handle.readable(),
                    handle.writable(), handle.write_queue_size(), uv_loop_alive(handle.native()->loop)};
}

class RawStreamNativeRejection : public RawStreamRequests, public ::testing::WithParamInterface<bool> {};
TEST_P(RawStreamNativeRejection, completedRequestsAndStreamRetainStateAfterNativeRejection) {
  pair();
  const bool use_static = GetParam();
  probe written, shutdown;
  writing.user_data(written);
  shutting.user_data(shutdown);
  int marker = 42;
  client.user_data(marker);
  const uv::buffer_view payload{storage.data(), 1};

  // Seed meaningful native request fields with real completed operations.
  ASSERT_TRUE(client.write(writing, payload, observe<write_request>));
  ASSERT_TRUE(client.shutdown(shutting, observe<shutdown_request>));
  until([&] { return written.calls == 1 && shutdown.calls == 1; });
  ASSERT_TRUE(written.outcome);
  ASSERT_TRUE(shutdown.outcome);
  ASSERT_FALSE(writing.pending());
  ASSERT_FALSE(shutting.pending());
  ASSERT_TRUE(client.initialized());
  ASSERT_FALSE(client.closing());
  ASSERT_FALSE(client.writable());
  EXPECT_EQ(writing.type(), uv::request_type::write);
  EXPECT_EQ(shutting.type(), uv::request_type::shutdown);
  EXPECT_EQ(writing.native()->handle, client.native_stream());
  EXPECT_EQ(shutting.native()->handle, client.native_stream());

  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> rejected_capture = token;
  int rejected_deliveries = 0;
  const auto write_before = request_state(writing);
  const auto stream_before_write = stream_state(client);
  const auto write_rejection = use_static
    ? client.write_static<observe<write_request>>(writing, payload)
    : client.write(writing, payload, [&, token](write_request&, uv::status) { ++rejected_deliveries; });
  // Valid callback/buffer, idle request, initialized non-closing handle: no
  // wrapper guard returns EPIPE. This rejection comes from uv_write itself.
  EXPECT_EQ(write_rejection.error(), uv::make_error_code(UV_EPIPE));
  EXPECT_EQ(request_state(writing), write_before);
  EXPECT_EQ(stream_state(client), stream_before_write);

  const auto shutdown_before = request_state(shutting);
  const auto stream_before_shutdown = stream_state(client);
  const auto shutdown_rejection = use_static
    ? client.shutdown_static<observe<shutdown_request>>(shutting)
    : client.shutdown(shutting, [&, token](shutdown_request&, uv::status) { ++rejected_deliveries; });
  // ENOTCONN likewise comes from uv_shutdown, after uvpp's guards have passed.
  EXPECT_EQ(shutdown_rejection.error(), uv::make_error_code(UV_ENOTCONN));
  EXPECT_EQ(request_state(shutting), shutdown_before);
  EXPECT_EQ(stream_state(client), stream_before_shutdown);
  token.reset();
  EXPECT_TRUE(rejected_capture.expired());

  loop.run(uv::run_mode::nowait);
  EXPECT_EQ(written.calls, 1);
  EXPECT_EQ(shutdown.calls, 1);
  EXPECT_EQ(rejected_deliveries, 0);
  EXPECT_EQ(request_state(writing), write_before);
  EXPECT_EQ(request_state(shutting), shutdown_before);
  EXPECT_EQ(stream_state(client), stream_before_shutdown);

  // Reuse both requests on another healthy stream, switching callback mode.
  probe connected_again;
  connecting.user_data(connected_again);
  ASSERT_TRUE(client2.connect_static<observe<connect_request>>(connecting, address()));
  until([&] { return connected_again.calls == 1 && accepted == 2; });
  ASSERT_TRUE(connected_again.outcome);
  const auto reused_write = use_static
    ? client2.write(writing, payload, observe<write_request>)
    : client2.write_static<observe<write_request>>(writing, payload);
  ASSERT_TRUE(reused_write);
  const auto reused_shutdown = use_static
    ? client2.shutdown(shutting, observe<shutdown_request>)
    : client2.shutdown_static<observe<shutdown_request>>(shutting);
  ASSERT_TRUE(reused_shutdown);
  until([&] { return written.calls == 2 && shutdown.calls == 2; });
  EXPECT_TRUE(written.outcome);
  EXPECT_TRUE(shutdown.outcome);
  EXPECT_FALSE(writing.pending());
  EXPECT_FALSE(shutting.pending());
  EXPECT_EQ(writing.user_data<probe>(), &written);
  EXPECT_EQ(shutting.user_data<probe>(), &shutdown);
  EXPECT_EQ(rejected_deliveries, 0);
}
INSTANTIATE_TEST_SUITE_P(CallbackModes, RawStreamNativeRejection, ::testing::Values(false, true),
  [](const ::testing::TestParamInfo<bool> &info) { return info.param ? "Static" : "Runtime"; });

TEST_F(RawStreamRequests, busyWritePreservesOriginalCallbackNativeStorageAndApplicationData) {
  pair();
  probe completed;
  writing.user_data(completed);
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> retained = token;
  auto *native = writing.native();
  ASSERT_TRUE(client.write(writing, uv::buffer_view{storage.data(), 1},
    [token](write_request &request, uv::status status) { observe(request, status); }));
  token.reset();
  ASSERT_TRUE(writing.pending());
  auto rejected = std::make_shared<int>(1);
  std::weak_ptr<int> rejected_weak = rejected;
  tcp uninitialized;
  EXPECT_EQ(uninitialized.write(writing, uv::buffer_view{}, [rejected](write_request&, uv::status) {}).error(),
            uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(client.write_static<observe<write_request>>(writing, uv::buffer_view{}).error(),
            uv::make_error_code(UV_EBUSY));
  rejected.reset();
  EXPECT_TRUE(rejected_weak.expired());
  EXPECT_FALSE(retained.expired());
  EXPECT_EQ(writing.native(), native);
  EXPECT_EQ(writing.user_data<probe>(), &completed);
  until([&] { return completed.calls == 1; });
  EXPECT_TRUE(completed.outcome);
  EXPECT_TRUE(retained.expired());
}

TEST_F(RawStreamRequests, runtimeWriteCanResubmitStaticThenRuntimeOnSameRequest) {
  pair();
  probe completed;
  writing.user_data(completed);
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> retired = token;
  ASSERT_TRUE(client.write(writing, uv::buffer_view{storage.data(), 1},
    [&, token](write_request &request, uv::status status) {
      EXPECT_TRUE(status);
      EXPECT_FALSE(request.pending());
      EXPECT_TRUE(client.write_static<observe<write_request>>(request, std::as_bytes(std::span{storage}.first(1))));
      EXPECT_TRUE(request.pending());
      EXPECT_FALSE(retired.expired());
    }));
  token.reset();
  until([&] { return completed.calls == 1; });
  EXPECT_TRUE(retired.expired());
  EXPECT_TRUE(completed.outcome);
  ASSERT_TRUE(client.write(writing, std::as_bytes(std::span{storage}.first(1)), observe<write_request>));
  until([&] { return completed.calls == 2; });
}

TEST_F(RawStreamRequests, connectBusyAndReentrantReuseAcrossHandlesKeepRequestIdentity) {
  listen();
  probe completed;
  connecting.user_data(completed);
  const auto addr = address();
  int runtime_calls = 0;
  ASSERT_TRUE(client.connect(connecting, addr, [&](connect_request &request, uv::status status) {
    ++runtime_calls;
    EXPECT_EQ(&request, &connecting);
    EXPECT_FALSE(request.pending());
    EXPECT_TRUE(status);
    EXPECT_TRUE(client2.connect_static<observe<connect_request>>(request, addr));
    EXPECT_TRUE(request.pending());
  }));
  EXPECT_TRUE(connecting.pending());
  EXPECT_EQ(client2.connect(connecting, addr, observe<connect_request>).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(client2.connect_static<observe<connect_request>>(connecting, addr).error(), uv::make_error_code(UV_EBUSY));
  until([&] { return completed.calls == 1 && accepted == 2; });
  EXPECT_EQ(runtime_calls, 1);
  EXPECT_TRUE(completed.outcome);
}

TEST_F(RawStreamRequests, shutdownBusyAndReentrantReuseAcrossStreamsReleaseSlotBeforeDelivery) {
  pair();
  connect_request second_connect;
  bool second_connected = false;
  ASSERT_TRUE(client2.connect(second_connect, address(), [&](connect_request&, uv::status status) {
    EXPECT_TRUE(status); second_connected = true;
  }));
  until([&] { return second_connected && accepted == 2; });
  probe completed;
  shutting.user_data(completed);
  ASSERT_TRUE(client.shutdown(shutting, [&](shutdown_request &request, uv::status status) {
    EXPECT_TRUE(status);
    EXPECT_FALSE(request.pending());
    EXPECT_TRUE(client2.shutdown_static<observe<shutdown_request>>(request));
    EXPECT_TRUE(request.pending());
  }));
  EXPECT_EQ(client2.shutdown(shutting, observe<shutdown_request>).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(client2.shutdown_static<observe<shutdown_request>>(shutting).error(), uv::make_error_code(UV_EBUSY));
  until([&] { return completed.calls == 1; });
  EXPECT_TRUE(completed.outcome);
}

TEST_F(RawStreamRequests, refusedConnectReportsNativeFailureAtItsActualBoundary) {
  server.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
  const auto target = address();
  // Reserve a distinct source port before releasing the destination, so the
  // client cannot pick that same port for its local endpoint afterwards.
  ASSERT_TRUE(client.bind(uv::ipv4{"127.0.0.1", 0}));
  ASSERT_NE(client.sockname().value().port(), target.port());
  bool server_closed = false;
  server.close([&](tcp&) { server_closed = true; });
  loop.run();
  ASSERT_TRUE(server_closed);

  // A bound-but-not-listening socket need not reject promptly on every backend.
  // Connect only after native close has released the destination socket.
  probe completed;
  connecting.user_data(completed);
  const auto submitted = client.connect_static<observe<connect_request>>(connecting, target);
  if (submitted) {
    EXPECT_TRUE(connecting.pending());
    until([&] { return completed.calls == 1; });
    if (connecting.pending()) {
      // until() has already reported the timeout. Drain cancellation while the
      // observer is still alive, rather than reading its default success status
      // or leaving its address for the fixture destructor to dereference.
      client.close();
      loop.run();
      ASSERT_FALSE(connecting.pending());
      return;
    }
    ASSERT_EQ(completed.calls, 1);
    EXPECT_FALSE(completed.outcome);
    EXPECT_EQ(completed.outcome.error(), uv::make_error_code(UV_ECONNREFUSED));
  } else {
    EXPECT_EQ(submitted.error(), uv::make_error_code(UV_ECONNREFUSED));
    EXPECT_EQ(completed.calls, 0);
  }
  EXPECT_FALSE(connecting.pending());
}

TEST_F(RawStreamRequests, closeRetainsWritePayloadOwnerUntilRequestCompletion) {
  pair();
  auto payload = std::make_shared<std::array<char, 32>>();
  std::weak_ptr<std::array<char, 32>> weak = payload;
  bool completed = false;
  ASSERT_TRUE(client.write(writing, uv::buffer_view{payload->data(), payload->size()},
    [&, payload](write_request &request, uv::status status) {
      EXPECT_FALSE(request.pending());
      EXPECT_FALSE(weak.expired());
      EXPECT_TRUE(status || status.error() == uv::make_error_code(UV_ECANCELED));
      completed = true;
    }));
  payload.reset();
  client.close();
  EXPECT_TRUE(writing.pending());
  EXPECT_FALSE(weak.expired());
  until([&] { return completed; });
  EXPECT_TRUE(weak.expired());
}

TEST_F(RawStreamRequests, closeDuringConnectKeepsCallbackUntilNativeCompletion) {
  listen();
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  bool completed = false;
  ASSERT_TRUE(client.connect(connecting, address(), [&, token](connect_request &request, uv::status status) {
    EXPECT_FALSE(request.pending());
    EXPECT_FALSE(weak.expired());
    EXPECT_TRUE(status || status.error() == uv::make_error_code(UV_ECANCELED));
    completed = true;
  }));
  token.reset();
  client.close();
  EXPECT_TRUE(connecting.pending());
  EXPECT_FALSE(weak.expired());
  until([&] { return completed; });
  EXPECT_TRUE(weak.expired());
}

TEST_F(RawStreamRequests, closeDuringShutdownKeepsCallbackUntilNativeCompletion) {
  pair();
  auto token = std::make_shared<int>(0);
  std::weak_ptr<int> weak = token;
  bool completed = false;
  ASSERT_TRUE(client.shutdown(shutting, [&, token](shutdown_request &request, uv::status status) {
    EXPECT_FALSE(request.pending());
    EXPECT_FALSE(weak.expired());
    EXPECT_TRUE(status || status.error() == uv::make_error_code(UV_ECANCELED));
    completed = true;
  }));
  token.reset();
  client.close();
  EXPECT_TRUE(shutting.pending());
  EXPECT_FALSE(weak.expired());
  until([&] { return completed; });
  EXPECT_TRUE(weak.expired());
}

struct throwing_callback {
  throwing_callback() = default;
  throwing_callback(const throwing_callback&) { throw std::runtime_error{"prepare callback"}; }
  template<class Request> void operator()(Request&, uv::status) const {}
};
TEST_F(RawStreamRequests, callbackPreparationExceptionsLeaveExistingRequestIntact) {
  pair();
  probe completed;
  writing.user_data(completed);
  ASSERT_TRUE(client.write(writing, uv::buffer_view{storage.data(), 1}, observe<write_request>));
  throwing_callback callback;
  EXPECT_THROW(client.write(writing, uv::buffer_view{storage.data(), 1}, callback), std::runtime_error);
  EXPECT_THROW(client.shutdown(shutting, callback), std::runtime_error);
  EXPECT_THROW(client.connect(connecting, address(), callback), std::runtime_error);
  EXPECT_TRUE(writing.pending());
  EXPECT_FALSE(shutting.pending());
  EXPECT_FALSE(connecting.pending());
  until([&] { return completed.calls == 1; });
}

TEST_F(RawStreamRequests, scatterGatherWriteAndShutdownDeliverBorrowedBytesThenEof) {
  pair();
  char first = 'a', second = 'b';
  std::array buffers{uv::buffer_view{&first, 1}, uv::buffer_view{&second, 1}};
  std::array<char, 2> received{};
  std::size_t count = 0;
  bool eof = false;
  ASSERT_TRUE(peer.read_start([&](tcp&, std::size_t) { return uv::buffer_view{storage.data(), storage.size()}; },
    [&](tcp&, uv::read_result read) {
      if (read.eof()) { eof = true; return; }
      ASSERT_TRUE(read);
      for (const auto byte : read.bytes()) {
        ASSERT_LT(count, received.size());
        received[count++] = static_cast<char>(byte);
      }
    }));
  probe written, shutdown;
  writing.user_data(written); shutting.user_data(shutdown);
  ASSERT_TRUE(client.write(writing, buffers, observe<write_request>));
  ASSERT_TRUE(client.shutdown_static<observe<shutdown_request>>(shutting));
  until([&] { return eof && written.calls == 1 && shutdown.calls == 1; });
  EXPECT_TRUE(written.outcome);
  EXPECT_TRUE(shutdown.outcome);
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(received, (std::array{'a', 'b'}));
}

TEST_F(RawStreamRequests, completionExceptionsTerminateAtNativeBoundary) {
  pair();
  EXPECT_DEATH({
    client.write(writing, uv::buffer_view{storage.data(), 1}, [](write_request&, uv::status) { throw 1; }).throw_if_error();
    loop.run();
  }, "");
}
}
