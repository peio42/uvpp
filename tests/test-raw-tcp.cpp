#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif
#ifndef _WIN32
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "gtest/gtest.h"
#include "uvpp/handles/tcp.hpp"

namespace {
using tcp = uv::raw::tcp;

void count_close(tcp &self) {
  EXPECT_TRUE(self.initialized());
  ++*self.user_data<int>();
}
void unexpected_close(tcp&) { ADD_FAILURE(); }

struct throwing_close {
  throwing_close() = default;
  throwing_close(const throwing_close&) { throw std::runtime_error{"callback preparation"}; }
  void operator()(tcp&) const {}
};

class RawTcp : public ::testing::Test {
protected:
  uv::loop loop;
  tcp handle;

  ~RawTcp() override {
    if (handle.initialized() && !handle.closing()) handle.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }
};

TEST_F(RawTcp, defaultStorageGuardsTcpOperationsWithoutRegistering) {
  static_assert(!std::is_copy_constructible_v<tcp>);
  static_assert(!std::is_move_constructible_v<tcp>);
  static_assert(std::is_nothrow_default_constructible_v<tcp>);
  static_assert(!std::is_convertible_v<tcp&, uv_tcp_t*>);
  static_assert(!std::is_convertible_v<tcp&, uv::handle_view>);
  static_assert(std::is_same_v<decltype(handle.sockname()), uv::result<uv::socket_address>>);
  static_assert(std::is_same_v<decltype(handle.bind(std::declval<const uv::ipv4&>())), uv::status>);
  static_assert(noexcept(handle.init(loop)));
  static_assert(noexcept(handle.sockname()));
  static_assert(noexcept(handle.peername()));
  EXPECT_FALSE(handle.initialized());
  EXPECT_FALSE(handle.active());
  EXPECT_FALSE(handle.closing());
  handle.ref();
  handle.unref();
  EXPECT_FALSE(handle.has_ref());
  const auto badf = uv::make_error_code(UV_EBADF);
  EXPECT_EQ(handle.open(static_cast<uv_os_sock_t>(-1)).error(), badf);
  EXPECT_EQ(handle.bind(uv::ipv4{"127.0.0.1", 0}).error(), badf);
  EXPECT_EQ(handle.bind(uv::ipv6{"::1", 0}, uv::tcp_bind_flag::ipv6_only).error(), badf);
  EXPECT_EQ(handle.sockname().error(), badf);
  EXPECT_EQ(handle.peername().error(), badf);
  EXPECT_EQ(handle.no_delay(true).error(), badf);
  EXPECT_EQ(handle.keep_alive(false).error(), badf);
  EXPECT_EQ(handle.simultaneous_accepts(true).error(), badf);
  EXPECT_EQ(handle.fileno().error(), badf);
  EXPECT_EQ(handle.send_buffer_size().error(), badf);
  EXPECT_EQ(handle.send_buffer_size(1024).error(), badf);
  EXPECT_EQ(handle.receive_buffer_size().error(), badf);
  EXPECT_EQ(handle.receive_buffer_size(1024).error(), badf);
#if UVPP_HAS_TCP_CLOSE_RESET
  EXPECT_EQ(handle.close_reset().error(), badf);
  EXPECT_EQ(handle.close_reset(unexpected_close).error(), badf);
  EXPECT_EQ(handle.close_reset_static<unexpected_close>().error(), badf);
#endif
  EXPECT_TRUE(loop.handles().empty());
}

TEST_F(RawTcp, preInitCloseDropsCallbackAndAllowsStableInit) {
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  handle.close();
  handle.close([token](tcp&) { ADD_FAILURE(); });
  token.reset();
  EXPECT_TRUE(weak.expired());
  handle.close_static<unexpected_close>();
  loop.run();
  EXPECT_FALSE(handle.initialized());
  int marker = 42;
  handle.user_data(marker);
  auto *native = handle.native();
  ASSERT_TRUE(handle.init(loop.view()));
  EXPECT_TRUE(handle.initialized());
  EXPECT_TRUE(handle.has_ref());
  EXPECT_FALSE(handle.active());
  EXPECT_FALSE(loop.alive());
  EXPECT_EQ(handle.native(), native);
  EXPECT_EQ(handle.user_data<int>(), &marker);
  EXPECT_EQ(&tcp::from_native(native), &handle);
  EXPECT_EQ(&tcp::from_native(handle.native_handle()), &handle);
  EXPECT_EQ(handle.native_handle()->loop, loop.native());
  EXPECT_EQ(loop.handles().size(), 1u);
}

TEST_F(RawTcp, bindQueriesAndOptionsReturnTypedNativeOutcomes) {
  ASSERT_TRUE(handle.init(loop));
  EXPECT_FALSE(handle.sockname());
  EXPECT_FALSE(handle.peername());
  EXPECT_TRUE(handle.no_delay(true));
  EXPECT_TRUE(handle.no_delay(false));
  EXPECT_TRUE(handle.keep_alive(false));
  EXPECT_TRUE(handle.simultaneous_accepts(true));
  EXPECT_TRUE(handle.simultaneous_accepts(false));
  const auto reenabled = handle.simultaneous_accepts(true);
#ifdef _WIN32
  // Windows cannot transition back from single to multiple pending accepts.
  EXPECT_EQ(reenabled.error(), uv::make_error_code(UV_ENOTSUP));
#else
  EXPECT_TRUE(reenabled);
#endif
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  const auto &view = handle;
  auto address = view.sockname();
  ASSERT_TRUE(address);
  EXPECT_TRUE(address.value().is_v4());
  EXPECT_EQ(address.value().to_string(), "127.0.0.1");
  EXPECT_GT(address.value().port(), 0);
  const auto port = address.value().port();
  EXPECT_FALSE(handle.peername());
  EXPECT_TRUE(handle.keep_alive(true, std::chrono::seconds{60}));
  EXPECT_TRUE(handle.keep_alive(false));
  EXPECT_EQ(handle.keep_alive(true).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.keep_alive(true, std::chrono::seconds{-1}).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.keep_alive(true, std::chrono::seconds{(std::numeric_limits<std::int64_t>::max)()}).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.bind(uv::ipv4{"127.0.0.1", 0}, 0x80000000u).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.sockname().value().port(), port);
  EXPECT_TRUE(handle.fileno());
  auto send = handle.send_buffer_size();
  auto receive = handle.receive_buffer_size();
  ASSERT_TRUE(send);
  ASSERT_TRUE(receive);
  EXPECT_GT(send.value(), 0);
  EXPECT_GT(receive.value(), 0);
  EXPECT_TRUE(handle.send_buffer_size(send.value()));
  EXPECT_TRUE(handle.receive_buffer_size(receive.value()));
}

TEST_F(RawTcp, duplicateInitPreservesBoundSocketAndLoop) {
  uv::loop other;
  ASSERT_TRUE(handle.init(loop));
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  const auto port = handle.sockname().value().port();
  const auto socket = handle.fileno().value();
  EXPECT_EQ(handle.init(other.view()).error(), uv::make_error_code(UV_EALREADY));
#if UVPP_HAS_TCP_INIT_EX
  EXPECT_EQ(handle.init(other, uv::tcp_socket_family::ipv6).error(), uv::make_error_code(UV_EALREADY));
#endif
  EXPECT_EQ(handle.native_handle()->loop, loop.native());
  EXPECT_EQ(handle.sockname().value().port(), port);
  EXPECT_EQ(handle.fileno().value(), socket);
  EXPECT_NO_THROW(other.close());
}

#if UVPP_HAS_TCP_INIT_EX
TEST_F(RawTcp, invalidFamilyFailureLeavesRetryableStorageAndApplicationData) {
  int marker = 42;
  handle.user_data(marker);
  auto *native = handle.native();
  const auto invalid = static_cast<uv::tcp_socket_family>(0x10000);
  EXPECT_EQ(handle.init(loop, invalid).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.init(loop.view(), invalid).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(handle.initialized());
  EXPECT_EQ(handle.native_handle()->loop, nullptr);
  EXPECT_EQ(handle.user_data<int>(), &marker);
  EXPECT_EQ(handle.native(), native);
  EXPECT_TRUE(loop.handles().empty());
  EXPECT_THROW((tcp{loop, invalid}), uv::error);
  EXPECT_THROW((tcp{loop.view(), invalid}), uv::error);
  EXPECT_TRUE(loop.handles().empty());
  ASSERT_TRUE(handle.init(loop.view(), uv::tcp_socket_family::ipv4));
  EXPECT_TRUE(handle.fileno());
  EXPECT_EQ(handle.user_data<int>(), &marker);
}

TEST_F(RawTcp, ipv6FamilyAndTypedBindPreserveAddressPayload) {
  auto initialized = handle.init(loop, uv::tcp_socket_family::ipv6);
  if (!initialized && (initialized.error().native() == UV_EAFNOSUPPORT ||
                       initialized.error().native() == UV_EPROTONOSUPPORT)) {
    GTEST_SKIP() << "IPv6 sockets unavailable";
  }
  ASSERT_TRUE(initialized);
  auto bound = handle.bind(uv::ipv6{"::1", 0}, uv::tcp_bind_flag::ipv6_only);
  if (!bound && (bound.error().native() == UV_EADDRNOTAVAIL || bound.error().native() == UV_EAFNOSUPPORT)) {
    GTEST_SKIP() << "IPv6 loopback unavailable";
  }
  ASSERT_TRUE(bound);
  auto address = handle.sockname();
  ASSERT_TRUE(address);
  EXPECT_TRUE(address.value().is_v6());
  EXPECT_GT(address.value().port(), 0);
}
#endif

struct native_socket {
  uv_os_sock_t value = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  ~native_socket() {
    if (value == static_cast<uv_os_sock_t>(-1)) return;
#ifdef _WIN32
    ::closesocket(value);
#else
    ::close(value);
#endif
  }
};

TEST_F(RawTcp, openTakesSocketOwnershipAndKeepsHandleIdentity) {
  native_socket socket;
  ASSERT_NE(socket.value, static_cast<uv_os_sock_t>(-1));
  ASSERT_TRUE(handle.init(loop));
  auto *native = handle.native();
  EXPECT_FALSE(handle.open(static_cast<uv_os_sock_t>(-1)));
  EXPECT_TRUE(handle.initialized());
  ASSERT_TRUE(handle.open(socket.value));
  socket.value = static_cast<uv_os_sock_t>(-1); // Native handle now owns it.
  EXPECT_EQ(handle.native(), native);
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  EXPECT_GT(handle.sockname().value().port(), 0);
}

TEST_F(RawTcp, closeReleasesListenerCapturesAndKeepsSentinelThroughCompletion) {
  ASSERT_TRUE(handle.init(loop));
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  handle.listen([token](tcp&, uv::status) { ADD_FAILURE(); });
  token.reset();
  EXPECT_FALSE(weak.expired());
  int closed = 0;
  handle.user_data(closed);
  handle.close_static<count_close>();
  EXPECT_TRUE(weak.expired());
  EXPECT_TRUE(handle.initialized());
  EXPECT_TRUE(handle.closing());
  const auto badf = uv::make_error_code(UV_EBADF);
  EXPECT_EQ(handle.bind(uv::ipv4{"127.0.0.1", 0}).error(), badf);
  EXPECT_EQ(handle.open(static_cast<uv_os_sock_t>(-1)).error(), badf);
  EXPECT_EQ(handle.no_delay(true).error(), badf);
  EXPECT_EQ(handle.keep_alive(false).error(), badf);
  EXPECT_EQ(handle.simultaneous_accepts(true).error(), badf);
  EXPECT_EQ(handle.sockname().error(), badf);
  EXPECT_EQ(handle.peername().error(), badf);
#if UVPP_HAS_TCP_CLOSE_RESET
  EXPECT_EQ(handle.close_reset().error(), badf);
#endif
  EXPECT_EQ(handle.init(loop).error(), uv::make_error_code(UV_EALREADY));
  loop.run();
  EXPECT_EQ(closed, 1);
  EXPECT_TRUE(handle.initialized());
  EXPECT_EQ(handle.init(loop).error(), uv::make_error_code(UV_EALREADY));
}

TEST_F(RawTcp, callbackPreparationFailureDoesNotStartClose) {
  ASSERT_TRUE(handle.init(loop));
  throwing_close callback;
  EXPECT_THROW(handle.close(callback), std::runtime_error);
#if UVPP_HAS_TCP_CLOSE_RESET
  EXPECT_THROW(handle.close_reset(callback), std::runtime_error);
#endif
  EXPECT_TRUE(handle.initialized());
  EXPECT_FALSE(handle.closing());
  EXPECT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
}

struct connected_pair {
  uv::loop loop;
  tcp server;
  tcp client;
  tcp peer;
  uv::connect_request request;
  uv::shutdown_request shutdown_request;
  bool shutdown_completed = false;
  bool accepted = false;
  bool connected = false;

  ~connected_pair() {
    for (tcp *handle : {&server, &client, &peer}) {
      if (handle->initialized() && !handle->closing()) handle->close();
    }
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }
  void start() {
    server.init(loop).throw_if_error();
    client.init(loop).throw_if_error();
    peer.init(loop).throw_if_error();
    server.bind(uv::ipv4{"127.0.0.1", 0}).throw_if_error();
    server.listen([&](tcp &self, uv::status status) {
      EXPECT_TRUE(status);
      if (!status) return;
      EXPECT_NO_THROW(self.accept(peer));
      accepted = true;
    });
    const auto address = server.sockname().value().to_v4();
    client.connect(request, address, [&](uv::connect_request &req, uv::status status) {
      EXPECT_EQ(&req, &request);
      EXPECT_TRUE(status);
      connected = true;
    });
    pump_until([&] { return accepted && connected; });
  }
  template<class Predicate>
  void pump_until(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!done() && std::chrono::steady_clock::now() < deadline) {
      loop.run(uv::run_mode::nowait);
      std::this_thread::yield();
    }
    EXPECT_TRUE(done());
  }
};

TEST(RawTcpConnected, peerAndLocalAddressesRetainPayloadAndRequestIdentity) {
  connected_pair pair;
  int marker = 42;
  pair.request.user_data(marker);
  ASSERT_NO_THROW(pair.start());
  EXPECT_EQ(pair.request.user_data<int>(), &marker);
  auto local = pair.client.sockname();
  auto remote = pair.client.peername();
  ASSERT_TRUE(local);
  ASSERT_TRUE(remote);
  EXPECT_EQ(remote.value().port(), pair.server.sockname().value().port());
  EXPECT_EQ(local.value().port(), pair.peer.peername().value().port());
}

#if UVPP_HAS_TCP_CLOSE_RESET
TEST_F(RawTcp, failedResetDropsRejectedCloseCallbackAndAllowsNormalClose) {
  ASSERT_TRUE(handle.init(loop));
  auto token = std::make_shared<int>(42);
  std::weak_ptr<int> weak = token;
  EXPECT_FALSE(handle.close_reset([token](tcp&) { ADD_FAILURE(); }));
  token.reset();
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(handle.closing());
  EXPECT_TRUE(handle.initialized());
  int closed = 0;
  handle.user_data(closed);
  handle.close_static<count_close>();
  loop.run();
  EXPECT_EQ(closed, 1);
}

TEST(RawTcpConnected, resetReleasesReadCapturesAndRetainsCloseCallbackUntilCompletion) {
  connected_pair pair;
  ASSERT_NO_THROW(pair.start());
  auto read = std::make_shared<int>(1);
  std::weak_ptr<int> released = read;
  pair.client.read_start([](tcp&, std::size_t) { return uv::buffer_view{}; },
                         [read](tcp&, uv::read_result) { ADD_FAILURE(); });
  read.reset();
  auto close = std::make_shared<int>(2);
  std::weak_ptr<int> retained = close;
  int closed = 0;
  ASSERT_TRUE(pair.client.close_reset([&, close](tcp &self) {
    EXPECT_TRUE(self.initialized());
    EXPECT_TRUE(self.closing());
    ++closed;
  }));
  close.reset();
  EXPECT_TRUE(released.expired());
  EXPECT_FALSE(retained.expired());
  pair.pump_until([&] { return closed == 1; });
  EXPECT_TRUE(retained.expired());
}

TEST(RawTcpConnected, resetWithoutCallbackStartsNormalNativeCloseCompletion) {
  connected_pair pair;
  ASSERT_NO_THROW(pair.start());
  ASSERT_TRUE(pair.client.close_reset());
  EXPECT_TRUE(pair.client.closing());
  EXPECT_TRUE(pair.client.initialized());
  pair.loop.run(uv::run_mode::nowait);
}

TEST(RawTcpConnected, resetStaticCompletesOnStableHandle) {
  connected_pair pair;
  ASSERT_NO_THROW(pair.start());
  int closed = 0;
  pair.client.user_data(closed);
  ASSERT_TRUE(pair.client.close_reset_static<count_close>());
  pair.pump_until([&] { return closed == 1; });
}

TEST(RawTcpConnected, resetFailureDuringShutdownPreservesReadSubscriptionAndRequestCallback) {
  connected_pair pair;
  ASSERT_NO_THROW(pair.start());
  auto read = std::make_shared<int>(1);
  std::weak_ptr<int> read_retained = read;
  int deliveries = 0;
  pair.client.read_start([](tcp&, std::size_t) { return uv::buffer_view{}; },
                         [&, read, count = 0](tcp &self, uv::read_result) mutable {
    deliveries = ++count;
    auto rejected = std::make_shared<int>(3);
    std::weak_ptr<int> released = rejected;
    EXPECT_EQ(self.close_reset([rejected](tcp&) { ADD_FAILURE(); }).error(), uv::make_error_code(UV_EINVAL));
    rejected.reset();
    EXPECT_TRUE(released.expired());
    EXPECT_FALSE(self.closing());
  });
  read.reset();
  auto pending = std::make_shared<int>(2);
  std::weak_ptr<int> request_retained = pending;
  pair.client.shutdown(pair.shutdown_request, [&pair, pending](uv::shutdown_request &request, uv::status status) {
    EXPECT_EQ(&request, &pair.shutdown_request);
    EXPECT_TRUE(status);
    pair.shutdown_completed = true;
  });
  pending.reset();
  // Deliver through the registered stream trampoline while shutdown is pending:
  // a failed reset must not change persistent-slot generation or lose its mutable callable.
  auto empty = uv_buf_init(nullptr, 0);
  const auto dispatch = pair.client.native_stream()->read_cb;
  dispatch(pair.client.native_stream(), 0, &empty);
  dispatch(pair.client.native_stream(), 0, &empty);
  EXPECT_EQ(deliveries, 2);
  EXPECT_FALSE(read_retained.expired());
  EXPECT_FALSE(request_retained.expired());
  pair.pump_until([&] { return pair.shutdown_completed; });
  EXPECT_TRUE(request_retained.expired());
  pair.client.close();
  EXPECT_TRUE(read_retained.expired());
}
#endif

#if (defined(__unix__) || defined(__APPLE__)) && UVPP_HAS_TCP_INIT_EX
int exhausted_family_init() {
  uv::loop loop;
  tcp handle;
  int marker = 42;
  handle.user_data(marker);
  rlimit saved{};
  if (getrlimit(RLIMIT_NOFILE, &saved)) return 1;
  auto limited = saved;
  limited.rlim_cur = 0;
  if (setrlimit(RLIMIT_NOFILE, &limited)) return 2;
  // Keep only native status initiation inside the descriptor-starved window.
  // Constructing a C++ exception here can force UBSan's vptr checker/symbolizer
  // to open /proc resources, masking the native failure with sanitizer errors.
  const auto failed = handle.init(loop, uv::tcp_socket_family::ipv4);
  if (setrlimit(RLIMIT_NOFILE, &saved)) return 3;
  if (failed.error().native() != UV_EMFILE || handle.initialized()) return 4;
  bool propagated = false;
  try { failed.throw_if_error(); }
  catch (const uv::error &error) { propagated = error.code().value() == UV_EMFILE; }
  if (!propagated) return 7;
  if (!loop.handles().empty() || handle.user_data<int>() != &marker) return 5;
  if (!handle.init(loop, uv::tcp_socket_family::ipv4)) return 6;
  handle.close();
  loop.run();
  loop.close();
  return 0;
}
TEST(RawTcpDeathTest, nativeSocketCreationFailureAfterRegistrationAllowsRetry) {
  // Other suites may have started libuv/sanitizer threads. Re-exec rather than
  // running native allocation and C++ exception handling in a fork-only child.
  const auto previous_style = GTEST_FLAG_GET(death_test_style);
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(std::exit(exhausted_family_init()), ::testing::ExitedWithCode(0), "");
  GTEST_FLAG_SET(death_test_style, previous_style);
}
#endif

} // namespace
