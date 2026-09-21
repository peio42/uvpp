#include "co-test-support.hpp"

#include <array>
#include <chrono>
#include <optional>

#include "uvpp/co/resource_scope.hpp"
#include "uvpp/co/sleep.hpp"
#include "uvpp/co/task_scope.hpp"
#include "uvpp/net/udp_socket.hpp"

using namespace std::chrono_literals;
using namespace uvpp::test;

TEST(UvppV3Coroutine, publicUdpCloseReportsCompletionThroughOps) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }

  uv::loop loop;
  uv::udp_socket socket(loop, uv::ipv4{"127.0.0.1", 0});
  bool explicit_result_ok = false;
  bool joined_closed_owner = false;

  auto closer = [&]() -> uv::co::task<void> {
    const auto result = co_await uv::ops::close(socket);
    explicit_result_ok = result.has_value();
    co_await socket.close();
    joined_closed_owner = true;
    loop.stop();
  };

  auto execution = uv::co::spawn(loop, closer());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(explicit_result_ok);
  EXPECT_TRUE(joined_closed_owner);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, udpSocketSendReceiveAndScopedCleanup) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }

  uv::loop loop;
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  std::array<std::byte, 32> buffer{};
  std::size_t received = 0;
  bool peer_is_v4 = false;
  bool cleanup_completed = false;

  auto parent = [&]() -> uv::co::task<void> {
    auto receiver = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    auto sender = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    const auto receiver_address = receiver.view().local_address().to_v4();
    auto receive = [&]() -> uv::co::task<void> {
      auto result = co_await receiver.view().recv_from(buffer);
      received = result.size();
      peer_is_v4 = result.peer_address().is_v4();
    };
    auto send = [&]() -> uv::co::task<void> {
      // Destination is copied into the awaiter; payload remains borrowed until
      // native send completion.
      co_await sender.view().send_to("udp", receiver_address);
    };
    tasks.spawn(receive());
    tasks.spawn(send());
    co_await tasks.join();
    co_await resources.finish();
    cleanup_completed = true;
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(received, 3U);
  EXPECT_TRUE(peer_is_v4);
  EXPECT_TRUE(cleanup_completed);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, udpSocketPermitsOneReceiveAndOneSend) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }

  uv::loop loop;
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  std::array<std::byte, 16> socket_buffer{};
  std::array<std::byte, 16> peer_buffer{};
  std::string socket_received;
  std::string peer_received;

  auto parent = [&]() -> uv::co::task<void> {
    auto socket = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    auto peer = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    const auto socket_address = socket.view().local_address().to_v4();
    const auto peer_address = peer.view().local_address().to_v4();

    auto receive_on_socket = [&]() -> uv::co::task<void> {
      const auto result = co_await socket.view().recv_from(socket_buffer);
      socket_received.assign(reinterpret_cast<const char *>(socket_buffer.data()), result.size());
    };
    auto receive_on_peer = [&]() -> uv::co::task<void> {
      const auto result = co_await peer.view().recv_from(peer_buffer);
      peer_received.assign(reinterpret_cast<const char *>(peer_buffer.data()), result.size());
    };
    auto send_from_socket = [&]() -> uv::co::task<void> {
      co_await socket.view().send_to("outbound", peer_address);
    };
    auto send_from_peer = [&]() -> uv::co::task<void> {
      co_await peer.view().send_to("inbound", socket_address);
    };

    tasks.spawn(receive_on_socket());
    tasks.spawn(receive_on_peer());
    tasks.spawn(send_from_socket());
    tasks.spawn(send_from_peer());
    co_await tasks.join();
    co_await resources.finish();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(socket_received, "inbound");
  EXPECT_EQ(peer_received, "outbound");
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, udpSocketRejectsSimultaneousReceive) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }

  uv::loop loop;
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  std::array<std::byte, 16> first_buffer{};
  std::array<std::byte, 16> second_buffer{};
  bool first_completed = false;
  int second_status = 0;

  auto parent = [&]() -> uv::co::task<void> {
    auto receiver = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    auto sender = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    const auto receiver_address = receiver.view().local_address().to_v4();
    auto first = [&]() -> uv::co::task<void> {
      (void)co_await receiver.view().recv_from(first_buffer);
      first_completed = true;
    };
    auto second = [&]() -> uv::co::task<void> {
      try {
        (void)co_await receiver.view().recv_from(second_buffer);
      } catch (const uv::error &error) {
        second_status = error.code().value();
      }
    };
    auto send = [&]() -> uv::co::task<void> {
      co_await sender.view().send_to("packet", receiver_address);
    };

    tasks.spawn(first());
    tasks.spawn(second());
    tasks.spawn(send());
    co_await tasks.join();
    co_await resources.finish();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(second_status, UV_EBUSY);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, udpSocketRejectsSimultaneousSend) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }

  uv::loop loop;
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  bool first_completed = false;
  int second_status = 0;

  auto parent = [&]() -> uv::co::task<void> {
    auto sender = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    auto receiver = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    const auto receiver_address = receiver.view().local_address().to_v4();
    auto first = [&]() -> uv::co::task<void> {
      co_await sender.view().send_to("first", receiver_address);
      first_completed = true;
    };
    auto second = [&]() -> uv::co::task<void> {
      try {
        co_await sender.view().send_to("second", receiver_address);
      } catch (const uv::error &error) {
        second_status = error.code().value();
      }
    };

    tasks.spawn(first());
    tasks.spawn(second());
    co_await tasks.join();
    co_await resources.finish();
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(second_status, UV_EBUSY);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Coroutine, udpSocketStopCancelsReceiveBeforeScopedCleanup) {
  if (!loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "loopback networking is not permitted in this environment";
  }

  uv::loop loop;
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  std::array<std::byte, 32> buffer{};
  int deliveries = 0;
  bool canceled = false;
  bool cleanup_completed = false;

  auto parent = [&]() -> uv::co::task<void> {
    auto receiver = resources.own(uv::udp_socket{loop, uv::ipv4{"127.0.0.1", 0}});
    const auto receiver_address = receiver.view().local_address().to_v4();
    auto receive = [&]() -> uv::co::task<void> {
      try {
        (void)co_await receiver.view().recv_from(buffer);
      } catch (const uv::error &error) {
        ++deliveries;
        canceled = error.code().value() == UV_ECANCELED;
      }
    };
    tasks.spawn(receive());
    tasks.request_stop();
    co_await tasks.join();
    // A datagram after cancellation must not recover the completed awaiter.
    // recv_from() released its native and cancellation slots before join resumed.
    {
      uv::udp_socket sender{loop, uv::ipv4{"127.0.0.1", 0}};
      co_await sender.send_to("late", receiver_address);
    }
    co_await resources.finish();
    cleanup_completed = true;
  };

  auto execution = uv::co::spawn(loop, parent());
  loop.run();
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(deliveries, 1);
  EXPECT_TRUE(canceled);
  EXPECT_TRUE(cleanup_completed);
  EXPECT_NO_THROW(loop.close());
}
