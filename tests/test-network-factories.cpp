#include "co-test-support.hpp"

#include <type_traits>

#include "uvpp/net/tcp_listener.hpp"
#include "uvpp/net/pipe_listener.hpp"
#include "uvpp/net/udp_socket.hpp"

using namespace uvpp::test;

static_assert(std::same_as<decltype(uv::ops::make_tcp_listener(
    std::declval<uv::loop &>(), std::declval<const uv::ipv4 &>())),
    uv::result<uv::tcp_listener>>);
static_assert(std::same_as<decltype(uv::ops::make_pipe_listener(
    std::declval<uv::loop &>(), "")), uv::result<uv::pipe_listener>>);
static_assert(std::same_as<decltype(uv::ops::make_udp_socket(
    std::declval<uv::loop &>(), std::declval<const uv::ipv6 &>())),
    uv::result<uv::udp_socket>>);
static_assert(!std::is_copy_constructible_v<uv::result<uv::tcp_listener>>);

namespace {
struct handle_counts {
  int total = 0;
  int closing = 0;
};
handle_counts count_handles(uv::loop &loop) {
  handle_counts counts;
  uv_walk(loop.native(), [](uv_handle_t *handle, void *data) {
    auto &counts = *static_cast<handle_counts *>(data);
    ++counts.total;
    if (uv_is_closing(handle)) ++counts.closing;
  }, &counts);
  return counts;
}

template<class Constructor, class Factory>
void check_failure(uv::loop &loop, Constructor construct, Factory factory, int expected) {
  const auto before = count_handles(loop);
  int thrown = 0;
  try { construct(); }
  catch (const uv::error &error) { thrown = error.code().value(); }
  EXPECT_EQ(thrown, expected);
  const auto result = factory();
  EXPECT_FALSE(result);
  EXPECT_EQ(result.error().native(), thrown);
  // Error delivery schedules close but never drives a nested loop. Both failed
  // handles remain initialized and closing until the next loop iteration.
  const auto pending = count_handles(loop);
  EXPECT_EQ(pending.total, before.total + 2);
  EXPECT_EQ(pending.closing, before.closing + 2);
  loop.run(uv::run_mode::nowait);
  const auto after = count_handles(loop);
  EXPECT_EQ(after.total, before.total);
  EXPECT_EQ(after.closing, before.closing);
}
}

TEST(NetworkFactories, TcpListenFailurePreservesErrorAndPendingClose) {
  if (!loopback_tcp_is_permitted()) GTEST_SKIP();
  uv::loop loop;
  uv::tcp_listener occupied{loop, uv::ipv4{"127.0.0.1", 0}};
  const auto address = occupied.local_address().to_v4();
  // TCP bind may defer EADDRINUSE until listen; both stages share rollback.
  check_failure(loop, [&] { uv::tcp_listener duplicate{loop, address, 8}; },
      [&] { return uv::ops::make_tcp_listener(loop, address, 8); }, UV_EADDRINUSE);
  occupied.request_close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(NetworkFactories, UdpBindFailurePreservesErrorAndPendingClose) {
  if (!loopback_tcp_is_permitted()) GTEST_SKIP();
  uv::loop loop;
  uv::udp_socket occupied{loop, uv::ipv4{"127.0.0.1", 0}};
  const auto address = occupied.local_address().to_v4();
  check_failure(loop, [&] { uv::udp_socket duplicate{loop, address}; },
      [&] { return uv::ops::make_udp_socket(loop, address); }, UV_EADDRINUSE);
  occupied.request_close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(NetworkFactories, PipeBindFailurePreservesErrorAndPendingClose) {
  if (!local_pipe_is_permitted()) GTEST_SKIP();
  const auto path = v3_pipe_path() + "-factory-occupied";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener occupied{loop, path};
  check_failure(loop, [&] { uv::pipe_listener duplicate{loop, path}; },
      [&] { return uv::ops::make_pipe_listener(loop, path); }, UV_EADDRINUSE);
  occupied.request_close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

template<class Address>
class NetworkFactoryAddress : public ::testing::Test {};
using Addresses = ::testing::Types<uv::ipv4, uv::ipv6>;
TYPED_TEST_SUITE(NetworkFactoryAddress, Addresses);

TYPED_TEST(NetworkFactoryAddress, BindValidationFailureClosesInitializedTcpAndUdp) {
  // The address wrappers also accept native values. AF_UNSPEC exercises libuv's
  // deterministic validation failure without depending on host interfaces.
  TypeParam address = [] {
    if constexpr (std::same_as<TypeParam, uv::ipv4>) return TypeParam{sockaddr_in{}};
    else return TypeParam{sockaddr_in6{}};
  }();
  uv::loop loop;
  check_failure(loop, [&] { uv::tcp_listener invalid{loop, address}; },
      [&] { return uv::ops::make_tcp_listener(loop, address); }, UV_EINVAL);
  check_failure(loop, [&] { uv::udp_socket invalid{loop, address}; },
      [&] { return uv::ops::make_udp_socket(loop, address); }, UV_EINVAL);
  EXPECT_NO_THROW(loop.close());
}

TYPED_TEST(NetworkFactoryAddress, SuccessfulResultsTransferStableOwners) {
  const auto address = TypeParam{std::same_as<TypeParam, uv::ipv4> ? "127.0.0.1" : "::1", 0};
  uv::loop loop;
  auto tcp = uv::ops::make_tcp_listener(loop, address, 8);
  if (!tcp) {
    const auto code = tcp.error().native();
    loop.run();
    loop.close();
    if (code == UV_EAFNOSUPPORT || code == UV_EADDRNOTAVAIL || code == UV_EPERM)
      GTEST_SKIP() << "loopback address unavailable";
    FAIL() << tcp.error().name();
  }
  auto *tcp_native = tcp.value().native();
  auto listener = std::move(tcp).value();
  EXPECT_EQ(listener.native(), tcp_native);
  EXPECT_EQ(tcp.value().native(), nullptr);
  EXPECT_TRUE(listener.has_execution_loop(loop));
  EXPECT_GT(listener.local_address().port(), 0);

  auto udp = uv::ops::make_udp_socket(loop, address);
  ASSERT_TRUE(udp);
  auto *udp_native = udp.value().native();
  auto socket = std::move(udp).value();
  EXPECT_EQ(socket.native(), udp_native);
  EXPECT_EQ(udp.value().native(), nullptr);
  EXPECT_TRUE(socket.has_execution_loop(loop));
  EXPECT_GT(socket.local_address().port(), 0);
  listener.request_close();
  socket.request_close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(NetworkFactories, PipeResultMovesStableStorageAndDiscardSchedulesClose) {
  if (!local_pipe_is_permitted()) GTEST_SKIP();
  const auto path = v3_pipe_path() + "-factory-move";
  std::filesystem::remove(path);
  uv::loop loop;
  {
    auto result = uv::ops::make_pipe_listener(loop, path);
    ASSERT_TRUE(result);
    auto *native = result.value().native();
    auto *data = native->data;
    auto moved_result = std::move(result);
    auto owner = std::move(moved_result).value();
    EXPECT_EQ(owner.native(), native);
    EXPECT_EQ(owner.native()->data, data);
    EXPECT_EQ(result.value().native(), nullptr);
    EXPECT_EQ(moved_result.value().native(), nullptr);
    EXPECT_TRUE(owner.has_execution_loop(loop));
  }
  EXPECT_EQ(count_handles(loop).closing, 1);
  loop.run();
  EXPECT_EQ(count_handles(loop).total, 0);
  // Discarding a successful result owns and closes its value just like an owner.
  {
    auto discarded = uv::ops::make_pipe_listener(loop, path);
    EXPECT_TRUE(discarded);
  }
  EXPECT_EQ(count_handles(loop).closing, 1);
  loop.run();
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}
