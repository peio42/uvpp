#include "co-test-support.hpp"

#include <array>
#include <type_traits>

#include "uvpp/co/resource_scope.hpp"
#include "uvpp/net/pipe_listener.hpp"
#include "uvpp/net/tcp_listener.hpp"
#include "uvpp/net/udp_socket.hpp"

using namespace uvpp::test;

static_assert(std::same_as<decltype(uv::ops::read_some(
    std::declval<uv::tcp_connection &>(), std::span<std::byte>{}).await_resume()),
    uv::result<uv::tcp_connection::read_some_result>>);
static_assert(std::same_as<decltype(uv::ops::write(
    std::declval<uv::pipe_connection_view>(), "").await_resume()), uv::status>);
static_assert(std::same_as<decltype(uv::ops::accept(
    std::declval<uv::tcp_listener &>()).await_resume()), uv::result<uv::tcp_connection>>);
static_assert(std::same_as<decltype(uv::ops::recv_from(
    std::declval<uv::udp_socket_view>(), std::span<std::byte>{}).await_resume()),
    uv::result<uv::udp_socket::recv_from_result>>);
static_assert(std::same_as<decltype(uv::ops::send_to(
    std::declval<uv::udp_socket_view>(), std::span<const std::byte>{},
    std::declval<const uv::ipv6 &>()).await_resume()), uv::status>);

// Exercise both stream families against real native completions.
template<class Pair>
class NetworkOpsStream : public ::testing::Test {};
using StreamPairs = ::testing::Types<connected_tcp_pair, connected_pipe_pair>;
TYPED_TEST_SUITE(NetworkOpsStream, StreamPairs);

TYPED_TEST(NetworkOpsStream, ResultsPreserveSlotsCancellationEofAndClosedErrors) {
  if constexpr (std::same_as<TypeParam, connected_tcp_pair>) {
    if (!loopback_tcp_is_permitted()) GTEST_SKIP();
  } else {
    if (!local_pipe_is_permitted()) GTEST_SKIP();
  }
  TypeParam pair;
  pair.connect();
  std::array<std::byte, 8> buffer{};
  int canceled = 0;
  auto reader = [&]() -> uv::co::task<void> {
    const auto canceled_read = co_await uv::ops::read_some(*pair.client, buffer);
    canceled = canceled_read.error().native();
  };
  auto first = uv::co::spawn(pair.loop, reader());
  auto contender = [&]() -> uv::co::task<void> {
    const auto busy_read = co_await uv::ops::read_some(*pair.client, buffer);
    EXPECT_EQ(busy_read.error().native(), UV_EBUSY);
    const auto busy_close = co_await uv::ops::close(*pair.client);
    EXPECT_EQ(busy_close.error().native(), UV_EBUSY);
  };
  auto second = uv::co::spawn(pair.loop, contender());
  EXPECT_TRUE(second.done());
  EXPECT_NO_THROW(second.rethrow_if_failed());
  first.request_stop();
  EXPECT_TRUE(first.done());
  EXPECT_NO_THROW(first.rethrow_if_failed());
  EXPECT_EQ(canceled, UV_ECANCELED);

  uv::write_request request;
  std::string payload{"reply"};
  pair.peer->write(request, std::as_bytes(std::span{payload.data(), payload.size()}),
      [&](uv::write_request &, uv::status status) {
        EXPECT_TRUE(status);
        pair.peer->close();
      });
  auto resumed = [&]() -> uv::co::task<void> {
    const auto invalid_read = co_await uv::ops::read_some(*pair.client, {});
    EXPECT_EQ(invalid_read.error().native(), UV_EINVAL);
    auto read = co_await uv::ops::read_some(*pair.client, buffer);
    EXPECT_TRUE(read);
    EXPECT_EQ(read.value().count(), payload.size());
    EXPECT_FALSE(read.value().eof());
    EXPECT_EQ(std::string_view(reinterpret_cast<char *>(buffer.data()), read.value().count()), payload);
    auto eof = co_await uv::ops::read_some(*pair.client, buffer);
    EXPECT_TRUE(eof);
    EXPECT_TRUE(eof.value().eof());
    co_await pair.client->close();
    const auto closed_read = co_await uv::ops::read_some(*pair.client, buffer);
    EXPECT_EQ(closed_read.error().native(), UV_EBADF);
    const auto closed_write = co_await uv::ops::write(*pair.client, "closed");
    EXPECT_EQ(closed_write.error().native(), UV_EBADF);
    pair.loop.stop();
  };
  auto execution = uv::co::spawn(pair.loop, resumed());
  pair.loop.run();
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  pair.close();
}

TYPED_TEST(NetworkOpsStream, WriteResultsExcludeConcurrentWritesAndRejectWrongLoop) {
  if constexpr (std::same_as<TypeParam, connected_tcp_pair>) {
    if (!loopback_tcp_is_permitted()) GTEST_SKIP();
  } else {
    if (!local_pipe_is_permitted()) GTEST_SKIP();
  }
  TypeParam pair;
  pair.connect();
  auto writer = [&]() -> uv::co::task<void> {
    EXPECT_TRUE(co_await uv::ops::write(*pair.client, "borrowed"));
    pair.loop.stop();
  };
  auto execution = uv::co::spawn(pair.loop, writer());
  auto contender = [&]() -> uv::co::task<void> {
    const auto busy_write = co_await uv::ops::write(*pair.client, "busy");
    EXPECT_EQ(busy_write.error().native(), UV_EBUSY);
  };
  auto second = uv::co::spawn(pair.loop, contender());
  EXPECT_NO_THROW(second.rethrow_if_failed());
  uv::loop other;
  auto wrong_loop = [&]() -> uv::co::task<void> {
    bool rejected = false;
    try { (void)co_await uv::ops::write(*pair.client, "wrong loop"); }
    catch (const std::logic_error &) { rejected = true; }
    EXPECT_TRUE(rejected);
  };
  auto wrong = uv::co::spawn(other, wrong_loop());
  EXPECT_NO_THROW(wrong.rethrow_if_failed());
  EXPECT_NO_THROW(other.close());
  execution.request_stop(); // Submitted writes still deliver actual native completion.
  pair.loop.run();
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  pair.close();
}

TEST(NetworkOps, UdpResultsPreserveDatagramsBusyCancellationAndNativeSubmissionFailure) {
  if (!loopback_tcp_is_permitted()) GTEST_SKIP();
  uv::loop loop;
  auto receiver = std::move(uv::ops::make_udp_socket(loop, uv::ipv4{"127.0.0.1", 0})).value();
  auto sender = std::move(uv::ops::make_udp_socket(loop, uv::ipv4{"127.0.0.1", 0})).value();
  const auto address = receiver.local_address().to_v4();
  const auto sender_port = sender.local_address().port();
  std::array<std::byte, 2> buffer{};
  auto canceled = [&]() -> uv::co::task<void> {
    const auto canceled_receive = co_await uv::ops::recv_from(receiver, buffer);
    EXPECT_EQ(canceled_receive.error().native(), UV_ECANCELED);
  };
  auto waiting = uv::co::spawn(loop, canceled());
  auto busy = [&]() -> uv::co::task<void> {
    const auto busy_receive = co_await uv::ops::recv_from(receiver, buffer);
    EXPECT_EQ(busy_receive.error().native(), UV_EBUSY);
  };
  auto competing = uv::co::spawn(loop, busy());
  EXPECT_NO_THROW(competing.rethrow_if_failed());
  waiting.request_stop();
  EXPECT_TRUE(waiting.done());
  EXPECT_NO_THROW(waiting.rethrow_if_failed());
  auto receive = [&]() -> uv::co::task<void> {
    auto packet = co_await uv::ops::recv_from(receiver, buffer);
    EXPECT_TRUE(packet);
    EXPECT_EQ(packet.value().size(), 2U);
    EXPECT_TRUE(packet.value().partial());
    EXPECT_EQ(packet.value().peer_address().port(), sender_port);
    auto empty = co_await uv::ops::recv_from(receiver, buffer);
    EXPECT_TRUE(empty);
    EXPECT_EQ(empty.value().size(), 0U);
    co_await receiver.close();
    const auto closed_receive = co_await uv::ops::recv_from(receiver, buffer);
    EXPECT_EQ(closed_receive.error().native(), UV_EBADF);
  };
  auto send = [&]() -> uv::co::task<void> {
    // A zero destination port fails in uv_udp_send submission on this backend.
    EXPECT_FALSE(co_await uv::ops::send_to(sender, "invalid", uv::ipv4{"127.0.0.1", 0}));
    EXPECT_TRUE(co_await uv::ops::send_to(sender, "packet", address));
    EXPECT_TRUE(co_await uv::ops::send_to(sender, std::span<const std::byte>{}, address));
    co_await sender.close();
    const auto closed_send = co_await uv::ops::send_to(sender, "closed", address);
    EXPECT_EQ(closed_send.error().native(), UV_EBADF);
  };
  auto receiving = uv::co::spawn(loop, receive());
  auto sending = uv::co::spawn(loop, send());
  loop.run();
  EXPECT_TRUE(receiving.done());
  EXPECT_TRUE(sending.done());
  EXPECT_NO_THROW(receiving.rethrow_if_failed());
  EXPECT_NO_THROW(sending.rethrow_if_failed());
  EXPECT_NO_THROW(loop.close());
}

TYPED_TEST(NetworkOpsStream, AcceptResultsTransferOwnersAndWaitForProvisionalCleanup) {
  constexpr bool tcp = std::same_as<TypeParam, connected_tcp_pair>;
  if constexpr (tcp) {
    if (!loopback_tcp_is_permitted()) GTEST_SKIP();
  } else {
    if (!local_pipe_is_permitted()) GTEST_SKIP();
  }
  using Listener = std::conditional_t<tcp, uv::tcp_listener, uv::pipe_listener>;
  using Connection = std::conditional_t<tcp, uv::tcp_connection, uv::pipe_connection>;
  uv::loop loop;
  const auto path = v3_pipe_path() + "-ops";
  std::unique_ptr<Listener> listener;
  if constexpr (tcp) {
    listener = std::make_unique<Listener>(std::move(
        uv::ops::make_tcp_listener(loop, uv::ipv4{"127.0.0.1", 0})).value());
  } else {
    std::filesystem::remove(path);
    listener = std::make_unique<Listener>(std::move(
        uv::ops::make_pipe_listener(loop, path, true, 8)).value());
  }
  auto canceled = [&]() -> uv::co::task<void> {
    auto result = co_await uv::ops::accept(*listener);
    EXPECT_EQ(result.error().native(), UV_ECANCELED);
  };
  auto waiting = uv::co::spawn(loop, canceled());
  auto busy = [&]() -> uv::co::task<void> {
    const auto busy_accept = co_await uv::ops::accept(*listener);
    EXPECT_EQ(busy_accept.error().native(), UV_EBUSY);
  };
  auto competing = uv::co::spawn(loop, busy());
  EXPECT_TRUE(competing.done());
  EXPECT_NO_THROW(competing.rethrow_if_failed());
  waiting.request_stop();
  EXPECT_FALSE(waiting.done()); // Provisional native child still needs uv_close.
  loop.run(uv::run_mode::nowait);
  EXPECT_TRUE(waiting.done());
  EXPECT_NO_THROW(waiting.rethrow_if_failed());
  using State = std::conditional_t<tcp, uv::detail::tcp_listener_state,
      uv::detail::pipe_listener_state>;
  auto native_failure = [&]() -> uv::co::task<void> {
    const auto failed_accept = co_await uv::ops::accept(*listener);
    EXPECT_EQ(failed_accept.error().native(), UV_ECONNABORTED);
  };
  auto failed = uv::co::spawn(loop, native_failure());
  State::on_connection(reinterpret_cast<uv_stream_t *>(listener->native()), UV_ECONNABORTED);
  EXPECT_FALSE(failed.done());
  loop.run(uv::run_mode::nowait);
  EXPECT_TRUE(failed.done());
  EXPECT_NO_THROW(failed.rethrow_if_failed());
  auto throwing_failure = [&]() -> uv::co::task<void> {
    bool observed = false;
    try { (void)co_await listener->accept(); }
    catch (const uv::error &error) { observed = error.code().value() == UV_ECONNABORTED; }
    EXPECT_TRUE(observed);
  };
  auto throwing = uv::co::spawn(loop, throwing_failure());
  State::on_connection(reinterpret_cast<uv_stream_t *>(listener->native()), UV_ECONNABORTED);
  EXPECT_FALSE(throwing.done());
  loop.run(uv::run_mode::nowait);
  EXPECT_TRUE(throwing.done());
  EXPECT_NO_THROW(throwing.rethrow_if_failed());

  auto server = [&]() -> uv::co::task<void> {
    auto result = co_await uv::ops::accept(*listener);
    EXPECT_TRUE(result);
    auto connection = std::move(result).value();
    auto *native = connection.native();
    auto moved = std::move(connection);
    EXPECT_EQ(moved.native(), native);
    uv::co::resource_scope resources{loop};
    auto scoped = resources.own(std::move(moved));
    std::array<std::byte, 8> buffer{};
    auto read = co_await uv::ops::read_some(scoped.view(), buffer);
    EXPECT_TRUE(read);
    EXPECT_EQ(read.value().count(), 5U);
    co_await resources.finish();
    co_await listener->close();
    const auto closed_accept = co_await uv::ops::accept(*listener);
    EXPECT_EQ(closed_accept.error().native(), UV_EBADF);
  };
  auto client = [&]() -> uv::co::task<void> {
    auto connection = co_await [&]() {
      if constexpr (tcp) return Connection::connect(listener->local_address().to_v4());
      else return Connection::connect(path);
    }();
    uv::co::resource_scope resources{loop};
    auto scoped = resources.own(std::move(connection));
    EXPECT_TRUE(co_await uv::ops::write(scoped.view(), "hello"));
    co_await resources.finish();
  };
  auto accepting = uv::co::spawn(loop, server());
  auto connecting = uv::co::spawn(loop, client());
  loop.run();
  EXPECT_TRUE(accepting.done());
  EXPECT_TRUE(connecting.done());
  EXPECT_NO_THROW(accepting.rethrow_if_failed());
  EXPECT_NO_THROW(connecting.rethrow_if_failed());
  EXPECT_NO_THROW(loop.close());
  if constexpr (!tcp) std::filesystem::remove(path);
}

TEST(NetworkOps, UdpBorrowedViewsShareSlotsWithMemberOperations) {
  if (!loopback_tcp_is_permitted()) GTEST_SKIP();
  uv::loop loop;
  uv::co::resource_scope resources{loop};
  auto receiver = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
  auto sender = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
  const auto address = receiver.view().local_address().to_v4();
  std::array<std::byte, 8> buffer{};
  auto receive = [&]() -> uv::co::task<void> {
    auto read = co_await uv::ops::recv_from(receiver.view(), buffer);
    EXPECT_TRUE(read);
    EXPECT_EQ(read.value().size(), 4U);
    EXPECT_EQ((co_await receiver.view().recv_from(buffer)).size(), 5U);
  };
  auto send = [&]() -> uv::co::task<void> {
    co_await sender.view().send_to("four", address);
    EXPECT_TRUE(co_await uv::ops::send_to(sender.view(), "seven", address));
  };
  auto busy = [&]() -> uv::co::task<void> {
    const auto busy_send = co_await uv::ops::send_to(sender.view(), "busy", address);
    EXPECT_EQ(busy_send.error().native(), UV_EBUSY);
  };
  auto parent = [&]() -> uv::co::task<void> {
    auto receiving = uv::co::spawn(loop, receive());
    auto sending = uv::co::spawn(loop, send());
    auto competing = uv::co::spawn(loop, busy());
    co_await competing.join();
    co_await sending.join();
    co_await receiving.join();
    competing.rethrow_if_failed();
    sending.rethrow_if_failed();
    receiving.rethrow_if_failed();
    co_await resources.finish();
  };
  auto execution = uv::co::spawn(loop, parent());
  loop.run();
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_NO_THROW(loop.close());
}
