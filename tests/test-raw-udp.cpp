#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <type_traits>

#ifndef _WIN32
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "gtest/gtest.h"
#include "uvpp/handles/udp.hpp"

namespace {
using udp = uv::raw::udp;
void close_count(udp &handle) { ++*handle.user_data<int>(); }
void unexpected_send(uv::raw::udp_send_request&, uv::status) { ADD_FAILURE(); }

class RawUdp : public ::testing::Test {
protected:
  uv::loop loop;
  udp handle;
  ~RawUdp() override {
    handle.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }
};

TEST_F(RawUdp, defaultStorageGuardsSocketOperationsAndObservations) {
  static_assert(std::is_nothrow_default_constructible_v<udp>);
  static_assert(!std::is_copy_constructible_v<udp>);
  static_assert(!std::is_move_constructible_v<udp>);
  static_assert(!std::is_convertible_v<udp&, uv_udp_t*>);
  static_assert(!std::is_convertible_v<udp&, uv::handle_view>);
  static_assert(std::is_same_v<decltype(handle.sockname()), uv::result<uv::socket_address>>);
  static_assert(std::is_same_v<decltype(handle.set_ttl(1)), uv::status>);
  static_assert(noexcept(handle.init(loop)));
  static_assert(noexcept(handle.sockname()));
  const auto badf = uv::make_error_code(UV_EBADF);
  EXPECT_FALSE(handle.initialized());
  EXPECT_FALSE(handle.active()); EXPECT_FALSE(handle.closing());
  EXPECT_FALSE(handle.has_ref());
  handle.ref(); handle.unref(); handle.close();
  EXPECT_TRUE(loop.handles().empty());
  EXPECT_EQ(handle.open(static_cast<uv_os_sock_t>(-1)).error(), badf);
  EXPECT_EQ(handle.bind(uv::ipv4{"127.0.0.1", 0}).error(), badf);
  EXPECT_EQ(handle.bind(uv::ipv6{"::1", 0}, uv::udp_bind_flag::ipv6_only).error(), badf);
  EXPECT_EQ(handle.sockname().error(), badf);
  EXPECT_EQ(handle.fileno().error(), badf);
  EXPECT_EQ(handle.send_buffer_size().error(), badf);
  EXPECT_EQ(handle.receive_buffer_size(4096).error(), badf);
  EXPECT_EQ(handle.set_broadcast(true).error(), badf);
  EXPECT_EQ(handle.set_ttl(64).error(), badf);
  EXPECT_EQ(handle.set_multicast_loop(true).error(), badf);
  EXPECT_EQ(handle.set_multicast_ttl(1).error(), badf);
  EXPECT_EQ(handle.set_multicast_interface().error(), badf);
  EXPECT_EQ(handle.set_multicast_interface("127.0.0.1").error(), badf);
  EXPECT_EQ(handle.set_membership("239.255.42.99", uv::membership::join).error(), badf);
  EXPECT_EQ(handle.set_membership("239.255.42.99", "127.0.0.1", uv::membership::join).error(), badf);
#if UVPP_HAS_UDP_SOURCE_MEMBERSHIP
  EXPECT_EQ(handle.set_source_membership("239.255.42.99", "127.0.0.1", uv::membership::join).error(), badf);
  EXPECT_EQ(handle.set_source_membership("239.255.42.99", "127.0.0.1", "127.0.0.1", uv::membership::join).error(), badf);
#endif
#if UVPP_HAS_UDP_CONNECT
  EXPECT_EQ(handle.connect(uv::ipv4{"127.0.0.1", 9}).error(), badf);
  EXPECT_EQ(handle.disconnect().error(), badf);
  EXPECT_EQ(handle.peername().error(), badf);
#endif
  EXPECT_EQ(handle.send_queue_size(), 0u);
  EXPECT_EQ(handle.send_queue_count(), 0u);
#if UVPP_HAS_UDP_USING_RECVMMSG
  EXPECT_FALSE(handle.using_recvmmsg());
#endif
  uv::raw::udp_send_request request;
  std::array<char, 1> data{};
  const uv::buffer_view buffer{data.data(), data.size()};
  EXPECT_EQ(handle.send(request, buffer, uv::ipv4{"127.0.0.1", 9}, unexpected_send).error(), badf);
  EXPECT_EQ(handle.send_now(buffer, uv::ipv4{"127.0.0.1", 9}).error_code(), badf);
  EXPECT_EQ(handle.receive_start([](udp&, std::size_t) { return uv::buffer_view{}; },
                                 [](udp&, uv::udp_receive_result) {}).error(), badf);
  EXPECT_EQ(handle.receive_stop().error(), badf);
}

TEST_F(RawUdp, repeatedInitKeepsNativeIdentityDataAndBoundSocket) {
  int marker = 42;
  handle.user_data(marker);
  auto *native = handle.native();
  ASSERT_TRUE(handle.init(loop.view()));
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}, uv::udp_bind_flag::reuse_address));
  const auto port = handle.sockname().value().port();
  uv::loop other;
  EXPECT_EQ(handle.init(other).error(), uv::make_error_code(UV_EALREADY));
#if UVPP_HAS_UDP_INIT_EX
  EXPECT_EQ(handle.init(loop, uv::udp_socket_family::ipv6).error(), uv::make_error_code(UV_EALREADY));
#endif
  EXPECT_EQ(handle.native(), native);
  EXPECT_EQ(handle.native()->loop, loop.native());
  EXPECT_EQ(handle.user_data<int>(), &marker);
  EXPECT_EQ(handle.sockname().value().port(), port);
  EXPECT_TRUE(other.handles().empty());
  EXPECT_NO_THROW(other.close());
  handle.close_static<close_count>();
  EXPECT_EQ(handle.init(loop).error(), uv::make_error_code(UV_EALREADY));
  loop.run();
  EXPECT_EQ(marker, 43);
  EXPECT_TRUE(handle.initialized());
  EXPECT_EQ(handle.init(loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(handle.bind(uv::ipv4{"127.0.0.1", 0}).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(handle.sockname().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(handle.set_ttl(64).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(handle.set_membership("239.255.42.99", uv::membership::join).error(), uv::make_error_code(UV_EBADF));
}

TEST_F(RawUdp, throwingConstructorsShareExplicitInitializationProtocol) {
  udp first{loop}, second{loop.view()};
  EXPECT_TRUE(first.initialized());
  EXPECT_TRUE(second.initialized());
  EXPECT_TRUE(first.bind(uv::ipv4{"127.0.0.1", 0}));
  EXPECT_TRUE(second.bind(uv::ipv4{"127.0.0.1", 0}));
  first.close(); second.close();
  loop.run();
}

#if UVPP_HAS_UDP_INIT_EX
TEST_F(RawUdp, invalidFamilyLeavesRetryableStorageAndApplicationData) {
  int marker = 42;
  handle.user_data(marker);
  auto *native = handle.native();
  const auto invalid = static_cast<uv::udp_socket_family>(0xff);
  EXPECT_EQ(handle.init(loop.view(), invalid).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(handle.initialized());
  EXPECT_EQ(handle.native()->loop, nullptr);
  EXPECT_EQ(handle.user_data<int>(), &marker);
  EXPECT_EQ(handle.native(), native);
  EXPECT_TRUE(loop.handles().empty());
  EXPECT_THROW((udp{loop, invalid}), uv::error);
  EXPECT_THROW((udp{loop.view(), invalid}), uv::error);
  EXPECT_TRUE(loop.handles().empty());
  ASSERT_TRUE(handle.init(loop, uv::udp_socket_family::ipv4));
  EXPECT_TRUE(handle.fileno());
  EXPECT_EQ(handle.user_data<int>(), &marker);
}

TEST_F(RawUdp, ipv6InitializationAndTypedBindRetainAddressPayload) {
  const auto initialized = handle.init(loop.view(), uv::udp_socket_family::ipv6);
  if (!initialized && (initialized.error().native() == UV_EAFNOSUPPORT || initialized.error().native() == UV_EPROTONOSUPPORT)) {
    GTEST_SKIP() << "IPv6 sockets unavailable";
  }
  ASSERT_TRUE(initialized);
  const auto bound = handle.bind(uv::ipv6{"::1", 0}, uv::udp_bind_flag::ipv6_only);
  if (!bound && (bound.error().native() == UV_EADDRNOTAVAIL || bound.error().native() == UV_EAFNOSUPPORT)) {
    GTEST_SKIP() << "IPv6 loopback unavailable";
  }
  ASSERT_TRUE(bound);
  const udp &observer = handle;
  const auto name = observer.sockname();
  ASSERT_TRUE(name);
  EXPECT_TRUE(name.value().is_v6());
  EXPECT_GT(name.value().port(), 0);
}
#endif

#if UVPP_HAS_UDP_INIT_EX && UVPP_HAS_UDP_RECVMMSG_FLAG
TEST_F(RawUdp, initializationFlagsShareStatusAndConstructorPaths) {
  int marker = 42;
  handle.user_data(marker);
  const auto invalid = static_cast<uv::udp_init_flag>(1u << 30);
  EXPECT_EQ(handle.init(loop, uv::udp_socket_family::ipv4, invalid).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(handle.initialized());
  EXPECT_EQ(handle.user_data<int>(), &marker);
  EXPECT_TRUE(loop.handles().empty());
  ASSERT_TRUE(handle.init(loop.view(), uv::udp_socket_family::ipv4, uv::udp_init_flag::recvmmsg));
  EXPECT_EQ(handle.init(loop, uv::udp_socket_family::ipv4, uv::udp_init_flag::none).error(), uv::make_error_code(UV_EALREADY));
  udp constructed{loop, uv::udp_socket_family::ipv4, uv::udp_init_flag::none};
  EXPECT_TRUE(constructed.fileno());
  constructed.close();
  loop.run();
}
#endif

struct native_socket {
  uv_os_sock_t value = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  ~native_socket() {
    if (value == static_cast<uv_os_sock_t>(-1)) return;
#ifdef _WIN32
    closesocket(value);
#else
    ::close(value);
#endif
  }
};
TEST_F(RawUdp, openAdoptsSocketOnlyOnSuccessAndKeepsAddressIdentity) {
  native_socket socket;
  ASSERT_NE(socket.value, static_cast<uv_os_sock_t>(-1));
  ASSERT_TRUE(handle.init(loop));
  auto *native = handle.native();
  EXPECT_FALSE(handle.open(static_cast<uv_os_sock_t>(-1)));
  EXPECT_TRUE(handle.initialized());
  ASSERT_TRUE(handle.open(socket.value));
  socket.value = static_cast<uv_os_sock_t>(-1);
  EXPECT_EQ(handle.native(), native);
  EXPECT_TRUE(handle.fileno());
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  EXPECT_GT(handle.sockname().value().port(), 0);
}

TEST_F(RawUdp, nativeSocketOptionsReturnStatusAndInvalidValuesDoNotCloseHandle) {
  ASSERT_TRUE(handle.init(loop));
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  const auto port = handle.sockname().value().port();
  EXPECT_TRUE(handle.set_broadcast(true));
  EXPECT_TRUE(handle.set_broadcast(false));
  EXPECT_TRUE(handle.set_ttl(64));
  EXPECT_EQ(handle.set_ttl(0).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.set_ttl(256).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(handle.set_multicast_loop(true));
  EXPECT_TRUE(handle.set_multicast_loop(false));
  EXPECT_TRUE(handle.set_multicast_ttl(1));
  EXPECT_TRUE(handle.set_multicast_interface("127.0.0.1"));
  EXPECT_TRUE(handle.set_multicast_interface());
  EXPECT_TRUE(handle.send_buffer_size(8192));
  EXPECT_TRUE(handle.receive_buffer_size(8192));
  EXPECT_GT(handle.send_buffer_size().value(), 0);
  EXPECT_GT(handle.receive_buffer_size().value(), 0);
  EXPECT_EQ(handle.sockname().value().port(), port);
  EXPECT_FALSE(handle.closing());
}

#if UVPP_HAS_UDP_CONNECT
TEST_F(RawUdp, connectedPeerAndDisconnectKeepTypedAddressOutcomes) {
  ASSERT_TRUE(handle.init(loop));
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  udp peer{loop};
  ASSERT_TRUE(peer.bind(uv::ipv4{"127.0.0.1", 0}));
  const auto target = peer.sockname().value().to_v4();
  EXPECT_FALSE(handle.peername());
  ASSERT_TRUE(handle.connect(target));
  const udp &observer = handle;
  ASSERT_TRUE(observer.peername());
  EXPECT_EQ(observer.peername().value().port(), target.port());
  ASSERT_TRUE(handle.disconnect());
  EXPECT_FALSE(handle.peername());
  peer.close();
  loop.run();
}
#endif

TEST_F(RawUdp, multicastTextValidationRejectsEmbeddedNulWithoutChangingBinding) {
  ASSERT_TRUE(handle.init(loop));
  ASSERT_TRUE(handle.bind(uv::ipv4{"127.0.0.1", 0}));
  const auto port = handle.sockname().value().port();
  const std::string embedded = std::string{"239.255.42.99"} + '\0' + "ignored";
  EXPECT_EQ(handle.set_membership(embedded, uv::membership::join).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.set_membership("239.255.42.99", embedded, uv::membership::join).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.set_multicast_interface(embedded).error(), uv::make_error_code(UV_EINVAL));
#if UVPP_HAS_UDP_SOURCE_MEMBERSHIP
  EXPECT_EQ(handle.set_source_membership("239.255.42.99", embedded, uv::membership::join).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.set_source_membership(embedded, "127.0.0.1", "127.0.0.1", uv::membership::join).error(), uv::make_error_code(UV_EINVAL));
#endif
  EXPECT_EQ(handle.set_membership("invalid-address", uv::membership::join).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.set_multicast_interface("invalid-address").error(), uv::make_error_code(UV_EINVAL));
  EXPECT_EQ(handle.sockname().value().port(), port);
}

TEST_F(RawUdp, multicastJoinLeavePreserveNativeBackendOutcomes) {
  ASSERT_TRUE(handle.init(loop));
  ASSERT_TRUE(handle.bind(uv::ipv4{"0.0.0.0", 0}, uv::udp_bind_flag::reuse_address));
  udp control{loop};
  ASSERT_TRUE(control.bind(uv::ipv4{"0.0.0.0", 0}, uv::udp_bind_flag::reuse_address));
  constexpr auto group = "239.255.42.99";
  constexpr auto network = "127.0.0.1";
  const auto native_join = uv_udp_set_membership(control.native(), group, network, UV_JOIN_GROUP);
  const auto wrapped_join = handle.set_membership(group, network, uv::membership::join);
  EXPECT_EQ(wrapped_join.error(), uv::make_error_code(native_join));
  if (native_join == 0 && wrapped_join) {
    const auto native_leave = uv_udp_set_membership(control.native(), group, network, UV_LEAVE_GROUP);
    EXPECT_EQ(handle.set_membership(group, network, uv::membership::leave).error(), uv::make_error_code(native_leave));
  }
#if UVPP_HAS_UDP_SOURCE_MEMBERSHIP
  const auto native_source = uv_udp_set_source_membership(control.native(), group, network, network, UV_JOIN_GROUP);
  const auto wrapped_source = handle.set_source_membership(group, network, network, uv::membership::join);
  EXPECT_EQ(wrapped_source.error(), uv::make_error_code(native_source));
  if (native_source == 0 && wrapped_source) {
    const auto native_leave = uv_udp_set_source_membership(control.native(), group, network, network, UV_LEAVE_GROUP);
    EXPECT_EQ(handle.set_source_membership(group, network, network, uv::membership::leave).error(), uv::make_error_code(native_leave));
  }
#endif
  control.close();
  loop.run();
}

#if !defined(_WIN32) && UVPP_HAS_UDP_INIT_EX
int exhausted_udp_init() {
  uv::loop loop;
  udp handle;
  int marker = 42;
  handle.user_data(marker);
  rlimit saved{};
  if (getrlimit(RLIMIT_NOFILE, &saved)) return 1;
  auto limited = saved;
  limited.rlim_cur = 0;
  if (setrlimit(RLIMIT_NOFILE, &limited)) return 2;
  const auto failed = handle.init(loop, uv::udp_socket_family::ipv4);
  if (setrlimit(RLIMIT_NOFILE, &saved)) return 3;
  if (failed.error().native() != UV_EMFILE || handle.initialized()) return 4;
  if (!loop.handles().empty() || handle.user_data<int>() != &marker) return 5;
  if (!handle.init(loop, uv::udp_socket_family::ipv4)) return 6;
  handle.close(); loop.run(); loop.close();
  return 0;
}
TEST(RawUdpDeathTest, nativeSocketCreationFailureLeavesUnregisteredStorageAndAllowsRetry) {
  const auto style = GTEST_FLAG_GET(death_test_style);
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(std::exit(exhausted_udp_init()), ::testing::ExitedWithCode(0), "");
  GTEST_FLAG_SET(death_test_style, style);
}
#endif
}
