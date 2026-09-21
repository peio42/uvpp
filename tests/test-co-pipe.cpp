#include "co-test-support.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "uvpp/co/resource_scope.hpp"
#include "uvpp/co/sleep.hpp"
#include "uvpp/co/task_scope.hpp"
#include "uvpp/net/pipe_listener.hpp"

using namespace std::chrono_literals;
using namespace uvpp::test;

TEST(UvppV3Coroutine, publicPipeCloseJoinsAndHasAnExplicitResultSurface) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }

  connected_pipe_pair pair;
  pair.connect();
  bool member_completed = false;
  bool explicit_result_ok = false;

  auto closer = [&]() -> uv::co::task<void> {
    co_await pair.client->close();
    member_completed = true;
    const auto result = co_await uv::ops::close(*pair.client);
    explicit_result_ok = result.has_value();
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, closer());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(member_completed);
  EXPECT_TRUE(explicit_result_ok);
  pair.close();
}

TEST(UvppV3Coroutine, pipeConnectionStreamsWithBorrowedBuffersAndScopedCleanup) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  connected_pipe_pair pair;
  pair.connect();

  std::array<char, 32> server_buffer{};
  std::array<char, 4> response{'p', 'o', 'n', 'g'};
  uv::write_request response_request;
  bool server_received = false;
  bool server_wrote = false;
  bool handler_completed = false;
  bool cleanup_completed = false;
  bool late_view_rejected = false;

  pair.peer->read_start(
      [&](uv::pipe &, std::size_t) {
        return uv::buffer_view{server_buffer.data(), server_buffer.size()};
      },
      [&](uv::pipe &stream, uv::read_result result) {
        if (result.eof()) {
          return;
        }
        ASSERT_TRUE(result);
        ASSERT_EQ(result.count(), 4);
        EXPECT_EQ((std::string_view{server_buffer.data(), 4}), "ping");
        server_received = true;
        stream.read_stop();
        stream.write(response_request, uv::buffer_view{response.data(), response.size()},
            [&](uv::write_request &, uv::status status) {
              EXPECT_TRUE(status);
              server_wrote = true;
              stream.close();
            });
      });

  uv::co::task_scope tasks(pair.loop);
  uv::co::resource_scope resources(pair.loop);
  auto parent = [&]() -> uv::co::task<void> {
    auto connection = resources.own(std::move(*pair.client));
    pair.client.reset();
    auto view = connection.view();
    auto handler = [&](uv::pipe_connection_view pipe) -> uv::co::task<void> {
      std::string request{"ping"};
      co_await pipe.write(request);
      std::array<std::byte, 16> buffer{};
      const auto result = co_await pipe.read_some(buffer);
      EXPECT_FALSE(result.eof());
      EXPECT_EQ(result.count(), 4U);
      EXPECT_EQ((std::string_view{reinterpret_cast<const char *>(buffer.data()), result.count()}),
          "pong");
      handler_completed = true;
    };
    tasks.spawn(handler(view));
    co_await tasks.join();
    co_await resources.finish();
    cleanup_completed = true;
    try {
      (void)view.write("late");
    } catch (const std::logic_error &) {
      late_view_rejected = true;
    }
    pair.listener.close();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(server_received);
  EXPECT_TRUE(server_wrote);
  EXPECT_TRUE(handler_completed);
  EXPECT_TRUE(cleanup_completed);
  EXPECT_TRUE(late_view_rejected);
  pair.close();
}

TEST(UvppV3Coroutine, pipeConnectionRejectsReadAndWriteFromAnotherLoop) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  connected_pipe_pair pair;
  pair.connect();

  uv::loop other_loop;
  std::array<std::byte, 8> buffer{};
  bool read_rejected = false;
  bool write_rejected = false;
  auto read = [&]() -> uv::co::task<void> {
    try {
      (void)co_await pair.client->read_some(buffer);
    } catch (const std::logic_error &) {
      read_rejected = true;
    }
  };
  auto write = [&]() -> uv::co::task<void> {
    try {
      co_await pair.client->write("wrong loop");
    } catch (const std::logic_error &) {
      write_rejected = true;
    }
  };

  auto read_execution = uv::co::spawn(other_loop, read());
  auto write_execution = uv::co::spawn(other_loop, write());

  EXPECT_TRUE(read_execution.done());
  EXPECT_TRUE(write_execution.done());
  EXPECT_NO_THROW(read_execution.rethrow_if_failed());
  EXPECT_NO_THROW(write_execution.rethrow_if_failed());
  EXPECT_TRUE(read_rejected);
  EXPECT_TRUE(write_rejected);

  other_loop.close();
  pair.close();
}

TEST(UvppV3Coroutine, pipeConnectionRejectsSimultaneousReadSome) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  connected_pipe_pair pair;
  pair.connect();

  std::array<std::byte, 8> first_buffer{};
  std::array<std::byte, 8> second_buffer{};
  bool first_saw_eof = false;
  int second_status = 0;
  auto first = [&]() -> uv::co::task<void> {
    first_saw_eof = (co_await pair.client->read_some(first_buffer)).eof();
    pair.loop.stop();
  };
  auto second = [&]() -> uv::co::task<void> {
    try {
      (void)co_await pair.client->read_some(second_buffer);
    } catch (const uv::error &error) {
      second_status = error.code().value();
    }
  };

  auto first_execution = uv::co::spawn(pair.loop, first());
  auto second_execution = uv::co::spawn(pair.loop, second());
  pair.peer->close();
  pair.loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_saw_eof);
  EXPECT_EQ(second_status, UV_EBUSY);
  pair.close();
}

TEST(UvppV3Coroutine, taskScopeStopCancelsPipeReadAndReleasesItsSlots) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  connected_pipe_pair pair;
  pair.connect();

  uv::co::task_scope scope(pair.loop);
  std::array<std::byte, 8> first_buffer{};
  std::array<std::byte, 8> second_buffer{};
  bool canceled = false;
  bool second_saw_eof = false;
  auto reader = [&]() -> uv::co::task<void> {
    try {
      (void)co_await pair.client->read_some(first_buffer);
    } catch (const uv::error &error) {
      canceled = error.code().value() == UV_ECANCELED;
    }
  };
  auto parent = [&]() -> uv::co::task<void> {
    scope.spawn(reader());
    scope.request_stop();
    co_await scope.join();
    pair.peer->close();
    second_saw_eof = (co_await pair.client->read_some(second_buffer)).eof();
    pair.loop.stop();
  };

  auto execution = uv::co::spawn(pair.loop, parent());
  pair.loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(canceled);
  EXPECT_TRUE(second_saw_eof);
  pair.close();
}

TEST(UvppV3Coroutine, pipeConnectionRejectsSimultaneousWrite) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  connected_pipe_pair pair;
  pair.connect();

  std::string first_data{"first"};
  std::string second_data{"second"};
  bool first_completed = false;
  int second_status = 0;
  auto first = [&]() -> uv::co::task<void> {
    co_await pair.client->write(first_data);
    first_completed = true;
    pair.loop.stop();
  };
  auto second = [&]() -> uv::co::task<void> {
    try {
      co_await pair.client->write(second_data);
    } catch (const uv::error &error) {
      second_status = error.code().value();
    }
  };

  auto first_execution = uv::co::spawn(pair.loop, first());
  auto second_execution = uv::co::spawn(pair.loop, second());
  pair.loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(second_status, UV_EBUSY);
  pair.close();
}

TEST(UvppV3Coroutine, pipeConnectionPermitsOneReadAndOneWrite) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  connected_pipe_pair pair;
  pair.connect();

  std::array<char, 16> peer_buffer{};
  std::array<std::byte, 16> client_buffer{};
  std::string request{"request"};
  std::string response{"response"};
  uv::write_request response_request;
  bool peer_received_request = false;
  bool write_completed = false;
  std::string client_received;

  pair.peer->read_start(
      [&](uv::pipe &, std::size_t) {
        return uv::buffer_view{peer_buffer.data(), peer_buffer.size()};
      },
      [&](uv::pipe &peer, uv::read_result result) {
        ASSERT_TRUE(result);
        EXPECT_EQ(result.count(), request.size());
        EXPECT_EQ((std::string_view{peer_buffer.data(),
                      static_cast<std::size_t>(result.count())}), request);
        peer_received_request = true;
        peer.read_stop();
        peer.write(response_request, std::as_bytes(std::span{response.data(), response.size()}),
            [](uv::write_request &, uv::status status) { EXPECT_TRUE(status); });
      });

  auto reader = [&]() -> uv::co::task<void> {
    const auto result = co_await pair.client->read_some(client_buffer);
    EXPECT_FALSE(result.eof());
    client_received.assign(reinterpret_cast<const char *>(client_buffer.data()), result.count());
    pair.loop.stop();
  };
  auto writer = [&]() -> uv::co::task<void> {
    co_await pair.client->write(request);
    write_completed = true;
  };

  auto read_execution = uv::co::spawn(pair.loop, reader());
  auto write_execution = uv::co::spawn(pair.loop, writer());
  pair.loop.run();

  EXPECT_NO_THROW(read_execution.rethrow_if_failed());
  EXPECT_NO_THROW(write_execution.rethrow_if_failed());
  EXPECT_TRUE(peer_received_request);
  EXPECT_TRUE(write_completed);
  EXPECT_EQ(client_received, response);
  pair.close();
}

TEST(UvppV3Coroutine, pipeConnectionReportsConnectCompletionFailure) {
  const auto path = v3_pipe_path() + "-missing";
  std::filesystem::remove(path);
  uv::loop loop;
  int status = 0;
  auto connect = [&]() -> uv::co::task<void> {
    try {
      (void)co_await uv::pipe_connection::connect(path);
    } catch (const uv::error &error) {
      status = error.code().value();
    }
  };

  auto execution = uv::co::spawn(loop, connect());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_LT(status, 0);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, ipcPipeTransfersTcpIntoAStableReceivedOwner) {
  if (!local_pipe_is_permitted() || !loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "local pipe or loopback TCP is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-ipc-handle";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener control_listener(loop, path, true);
  uv::tcp tcp_listener(loop);
  tcp_listener.bind(uv::ipv4{"127.0.0.1", 0});
  const uv::ipv4 tcp_address{"127.0.0.1", tcp_listener.sockname().port()};
  std::unique_ptr<uv::tcp> tcp_peer;
  tcp_listener.listen([&](uv::tcp &server, uv::status status) {
    EXPECT_TRUE(status);
    tcp_peer = std::make_unique<uv::tcp>(loop);
    EXPECT_NO_THROW(server.accept(*tcp_peer));
  });

  bool received_tcp = false;
  bool received_owner_was_stable = false;
  auto receiver = [&]() -> uv::co::task<void> {
    {
      auto control = co_await control_listener.accept();
      std::array<std::byte, 8> buffer{};
      auto incoming = co_await control.receive_handle(buffer);
      EXPECT_EQ(incoming.tcp_count(), 1);
      received_tcp = incoming.kind() == uv::receive_handle_kind::tcp;
      auto transferred = incoming.take_tcp();
      auto *before_move = transferred.native();
      auto moved = std::move(transferred);
      received_owner_was_stable = moved.native() == before_move;
      if (tcp_peer && !tcp_peer->closing()) {
        tcp_peer->close();
      }
    }
    tcp_listener.close();
    control_listener.request_close();
  };
  auto sender = [&]() -> uv::co::task<void> {
    {
      auto control = co_await uv::pipe_connection::connect(path, true);
      {
        auto source = co_await uv::tcp_connection::connect(tcp_address);
        co_await control.write_with_handle("h", source);
      }
    }
  };

  auto receiver_execution = uv::co::spawn(loop, receiver());
  auto sender_execution = uv::co::spawn(loop, sender());
  loop.run();

  EXPECT_NO_THROW(receiver_execution.rethrow_if_failed());
  EXPECT_NO_THROW(sender_execution.rethrow_if_failed());
  EXPECT_TRUE(received_tcp);
  EXPECT_TRUE(received_owner_was_stable);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, ipcReceiveHandleAccumulatesBytesUntilItsBufferFills) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-ipc-buffer-full";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path, true);
  int receive_status = 0;

  auto receiver = [&]() -> uv::co::task<void> {
    auto control = co_await listener.accept();
    std::array<std::byte, 1> buffer{};
    try {
      (void)co_await control.receive_handle(buffer);
    } catch (const uv::error &error) {
      receive_status = error.code().value();
    }
    listener.request_close();
  };
  auto sender = [&]() -> uv::co::task<void> {
    auto control = co_await uv::pipe_connection::connect(path, true);
    co_await control.write("x");
  };

  auto receiver_execution = uv::co::spawn(loop, receiver());
  auto sender_execution = uv::co::spawn(loop, sender());
  loop.run();

  EXPECT_NO_THROW(receiver_execution.rethrow_if_failed());
  EXPECT_NO_THROW(sender_execution.rethrow_if_failed());
  EXPECT_EQ(receive_status, UV_ENOBUFS);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, ipcReceiveHandleCancellationReleasesTheReadSlot) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-ipc-cancel";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path, true);
  bool canceled = false;
  bool reused_read_slot = false;

  auto receiver = [&]() -> uv::co::task<void> {
    auto control = co_await listener.accept();
    uv::co::task_scope scope(loop);
    std::array<std::byte, 8> canceled_buffer{};
    auto waiting = [&]() -> uv::co::task<void> {
      try {
        (void)co_await control.receive_handle(canceled_buffer);
      } catch (const uv::error &error) {
        canceled = error.code().value() == UV_ECANCELED;
      }
    };
    scope.spawn(waiting());
    scope.request_stop();
    co_await scope.join();
    std::array<std::byte, 8> read_buffer{};
    const auto read = co_await control.read_some(read_buffer);
    reused_read_slot = !read.eof() && read.count() == 1 &&
        read_buffer.front() == static_cast<std::byte>('x');
    listener.request_close();
  };
  auto sender = [&]() -> uv::co::task<void> {
    auto control = co_await uv::pipe_connection::connect(path, true);
    co_await uv::co::sleep_for(1ms);
    co_await control.write("x");
  };

  auto receiver_execution = uv::co::spawn(loop, receiver());
  auto sender_execution = uv::co::spawn(loop, sender());
  loop.run();

  EXPECT_NO_THROW(receiver_execution.rethrow_if_failed());
  EXPECT_NO_THROW(sender_execution.rethrow_if_failed());
  EXPECT_TRUE(canceled);
  EXPECT_TRUE(reused_read_slot);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, ipcPipeReceiveHandleRejectsAnActiveReadSome) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-ipc-read-slot";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path, true);
  bool read_canceled = false;
  int receive_status = 0;

  auto server = [&]() -> uv::co::task<void> {
    auto control = co_await listener.accept();
    uv::co::task_scope operations(loop);
    std::array<std::byte, 8> read_buffer{};
    std::array<std::byte, 8> receive_buffer{};
    auto read = [&]() -> uv::co::task<void> {
      try {
        (void)co_await control.read_some(read_buffer);
      } catch (const uv::error &error) {
        read_canceled = error.code().value() == UV_ECANCELED;
      }
    };
    auto receive = [&]() -> uv::co::task<void> {
      try {
        (void)co_await control.receive_handle(receive_buffer);
      } catch (const uv::error &error) {
        receive_status = error.code().value();
      }
    };

    operations.spawn(read());
    operations.spawn(receive());
    operations.request_stop();
    co_await operations.join();
    listener.request_close();
  };
  auto client = [&]() -> uv::co::task<void> {
    auto control = co_await uv::pipe_connection::connect(path, true);
    co_await uv::co::sleep_for(1ms);
  };

  auto server_execution = uv::co::spawn(loop, server());
  auto client_execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(server_execution.rethrow_if_failed());
  EXPECT_NO_THROW(client_execution.rethrow_if_failed());
  EXPECT_TRUE(read_canceled);
  EXPECT_EQ(receive_status, UV_EBUSY);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, pipeWriteWithHandleRejectsANonIpcPipe) {
  if (!local_pipe_is_permitted() || !loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "local pipe or loopback TCP is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-non-ipc-write-with-handle";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener pipe_listener(loop, path, false);
  uv::tcp tcp_listener(loop);
  tcp_listener.bind(uv::ipv4{"127.0.0.1", 0});
  const uv::ipv4 address{"127.0.0.1", tcp_listener.sockname().port()};
  std::unique_ptr<uv::tcp> peer;
  tcp_listener.listen([&](uv::tcp &server, uv::status status) {
    EXPECT_TRUE(status);
    peer = std::make_unique<uv::tcp>(loop);
    EXPECT_NO_THROW(server.accept(*peer));
  });
  int status = 0;

  auto run = [&]() -> uv::co::task<void> {
    auto pipe = co_await uv::pipe_connection::connect(path, false);
    auto source = co_await uv::tcp_connection::connect(address);
    try {
      co_await pipe.write_with_handle("x", source);
    } catch (const uv::error &error) {
      status = error.code().value();
    }
    if (peer && !peer->closing()) {
      peer->close();
    }
    tcp_listener.close();
    pipe_listener.request_close();
  };

  auto execution = uv::co::spawn(loop, run());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(status, UV_EINVAL);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, ipcPipeWriteWithHandleRejectsAnActiveWrite) {
  if (!local_pipe_is_permitted() || !loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "local pipe or loopback TCP is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-ipc-write-slot";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path, true);
  uv::tcp tcp_listener(loop);
  tcp_listener.bind(uv::ipv4{"127.0.0.1", 0});
  const uv::ipv4 address{"127.0.0.1", tcp_listener.sockname().port()};
  std::unique_ptr<uv::tcp> peer;
  tcp_listener.listen([&](uv::tcp &server, uv::status status) {
    EXPECT_TRUE(status);
    peer = std::make_unique<uv::tcp>(loop);
    EXPECT_NO_THROW(server.accept(*peer));
  });
  bool writers_finished = false;
  bool write_completed = false;
  int write_with_handle_status = 0;

  auto server = [&]() -> uv::co::task<void> {
    auto control = co_await listener.accept();
    while (!writers_finished) {
      co_await uv::co::sleep_for(1ms);
    }
    (void)control;
  };
  auto client = [&]() -> uv::co::task<void> {
    auto control = co_await uv::pipe_connection::connect(path, true);
    auto source = co_await uv::tcp_connection::connect(address);
    uv::co::task_scope writers(loop);
    auto write = [&]() -> uv::co::task<void> {
      co_await control.write("bytes");
      write_completed = true;
    };
    auto write_with_handle = [&]() -> uv::co::task<void> {
      try {
        co_await control.write_with_handle("handle", source);
      } catch (const uv::error &error) {
        write_with_handle_status = error.code().value();
      }
    };

    writers.spawn(write());
    writers.spawn(write_with_handle());
    co_await writers.join();
    writers_finished = true;
    if (peer && !peer->closing()) {
      peer->close();
    }
    tcp_listener.close();
    listener.request_close();
  };

  auto server_execution = uv::co::spawn(loop, server());
  auto client_execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(server_execution.rethrow_if_failed());
  EXPECT_NO_THROW(client_execution.rethrow_if_failed());
  EXPECT_TRUE(write_completed);
  EXPECT_EQ(write_with_handle_status, UV_EBUSY);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, pipeWriteWithHandleRejectsATcpFromAnotherLoop) {
  if (!local_pipe_is_permitted() || !loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "local pipe or loopback TCP is not permitted in this environment";
  }
  connected_tcp_pair source_pair;
  source_pair.connect();
  const auto path = v3_pipe_path() + "-cross-loop-write-with-handle";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path, true);
  bool rejected = false;

  auto run = [&]() -> uv::co::task<void> {
    auto pipe = co_await uv::pipe_connection::connect(path, true);
    try {
      co_await pipe.write_with_handle("x", *source_pair.client);
    } catch (const std::logic_error &) {
      rejected = true;
    }
    listener.request_close();
  };

  auto execution = uv::co::spawn(loop, run());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(rejected);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
  source_pair.close();
}

TEST(UvppV3Coroutine, pipeWriteWithHandleUsesTheTcpExportSlotAcrossPipes) {
  if (!local_pipe_is_permitted() || !loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "local pipe or loopback TCP is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-concurrent-handle-export";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path, true);
  uv::tcp tcp_listener(loop);
  tcp_listener.bind(uv::ipv4{"127.0.0.1", 0});
  const uv::ipv4 address{"127.0.0.1", tcp_listener.sockname().port()};
  std::unique_ptr<uv::tcp> peer;
  tcp_listener.listen([&](uv::tcp &server, uv::status status) {
    EXPECT_TRUE(status);
    peer = std::make_unique<uv::tcp>(loop);
    EXPECT_NO_THROW(server.accept(*peer));
  });
  bool first_completed = false;
  int second_status = 0;

  auto server = [&]() -> uv::co::task<void> {
    auto first = co_await listener.accept();
    auto second = co_await listener.accept();
    co_await uv::co::sleep_for(1ms);
    (void)first;
    (void)second;
  };
  auto client = [&]() -> uv::co::task<void> {
    auto first_pipe = co_await uv::pipe_connection::connect(path, true);
    auto second_pipe = co_await uv::pipe_connection::connect(path, true);
    auto source = co_await uv::tcp_connection::connect(address);
    uv::co::task_scope writers(loop);
    auto first = [&]() -> uv::co::task<void> {
      co_await first_pipe.write_with_handle("A", source);
      first_completed = true;
    };
    auto second = [&]() -> uv::co::task<void> {
      try {
        co_await second_pipe.write_with_handle("B", source);
      } catch (const uv::error &error) {
        second_status = error.code().value();
      }
    };
    writers.spawn(first());
    writers.spawn(second());
    co_await writers.join();
    if (peer && !peer->closing()) {
      peer->close();
    }
    tcp_listener.close();
    listener.request_close();
  };

  auto server_execution = uv::co::spawn(loop, server());
  auto client_execution = uv::co::spawn(loop, client());
  loop.run();

  EXPECT_NO_THROW(server_execution.rethrow_if_failed());
  EXPECT_NO_THROW(client_execution.rethrow_if_failed());
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(second_status, UV_EBUSY);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
}

TEST(UvppV3Coroutine, ipcPipeBackToBackHandleWritesPreserveBytesAndOwners) {
#if defined(__linux__)
  if (!local_pipe_is_permitted() || !loopback_tcp_is_permitted()) {
    GTEST_SKIP() << "local pipe or loopback TCP is not permitted in this environment";
  }
  const auto path = v3_pipe_path() + "-back-to-back-handles";
  std::filesystem::remove(path);
  uv::loop loop;
  uv::pipe_listener listener(loop, path, true);
  uv::tcp tcp_listener(loop);
  tcp_listener.bind(uv::ipv4{"127.0.0.1", 0});
  const uv::ipv4 address{"127.0.0.1", tcp_listener.sockname().port()};
  std::vector<std::unique_ptr<uv::tcp>> peers;
  tcp_listener.listen([&](uv::tcp &server, uv::status status) {
    EXPECT_TRUE(status);
    auto peer = std::make_unique<uv::tcp>(loop);
    EXPECT_NO_THROW(server.accept(*peer));
    peers.push_back(std::move(peer));
  });
  int received_count = 0;
  int valid_owners = 0;
  std::string received_bytes;
  std::vector<int> expected_local_ports;
  std::vector<int> received_local_ports;
  bool receiver_done = false;
  auto local_port = [](uv_tcp_t *handle) {
    sockaddr_storage storage{};
    int length = sizeof(storage);
    EXPECT_EQ(uv_tcp_getsockname(handle, reinterpret_cast<sockaddr *>(&storage), &length), 0);
    EXPECT_EQ(storage.ss_family, AF_INET);
    return uv::ipv4{*reinterpret_cast<sockaddr_in *>(&storage)}.port();
  };

  auto receiver = [&]() -> uv::co::task<void> {
    auto control = co_await listener.accept();
    while (received_count != 2) {
      std::array<std::byte, 8> buffer{};
      auto result = co_await control.receive_handle(buffer);
      received_bytes.append(reinterpret_cast<const char *>(buffer.data()),
          result.bytes_transferred());
      while (result.tcp_count() != 0) {
        auto connection = result.take_tcp();
        if (connection.native() != nullptr) {
          ++valid_owners;
          received_local_ports.push_back(local_port(connection.native()));
        }
        ++received_count;
      }
    }
    receiver_done = true;
    for (auto &peer : peers) {
      if (!peer->closing()) {
        peer->close();
      }
    }
    tcp_listener.close();
    listener.request_close();
  };
  auto sender = [&]() -> uv::co::task<void> {
    auto control = co_await uv::pipe_connection::connect(path, true);
    auto first = co_await uv::tcp_connection::connect(address);
    auto second = co_await uv::tcp_connection::connect(address);
    expected_local_ports.push_back(local_port(first.native()));
    expected_local_ports.push_back(local_port(second.native()));
    co_await control.write_with_handle("A", first);
    co_await control.write_with_handle("B", second);
    while (!receiver_done) {
      co_await uv::co::sleep_for(1ms);
    }
  };

  auto receiver_execution = uv::co::spawn(loop, receiver());
  auto sender_execution = uv::co::spawn(loop, sender());
  loop.run();

  EXPECT_NO_THROW(receiver_execution.rethrow_if_failed());
  EXPECT_NO_THROW(sender_execution.rethrow_if_failed());
  EXPECT_EQ(received_count, 2);
  EXPECT_EQ(valid_owners, 2);
  EXPECT_EQ(received_bytes, "AB");
  EXPECT_EQ(received_local_ports, expected_local_ports);
  EXPECT_NO_THROW(loop.close());
  std::filesystem::remove(path);
#else
  GTEST_SKIP() << "this IPC queue observation is Linux-specific";
#endif
}

TEST(UvppV3Coroutine, internalPipeCloseCompletionJoinsAndChecksAffinity) {
  if (!local_pipe_is_permitted()) {
    GTEST_SKIP() << "local pipe bind is not permitted in this environment";
  }
  connected_pipe_pair pair;
  pair.connect();

  uv::loop other_loop;
  bool wrong_loop_rejected = false;
  bool first_resumed = false;
  bool second_resumed = false;
  bool post_close_joined = false;
  bool first_initiated = false;
  bool second_initiated = false;
  auto wrong_loop = [&]() -> uv::co::task<void> {
    try {
      co_await uv::detail::close_completion(*pair.client);
    } catch (const std::logic_error &) {
      wrong_loop_rejected = true;
    }
  };
  auto first = [&]() -> uv::co::task<void> {
    auto close = uv::detail::close_completion(*pair.client);
    co_await close;
    first_initiated = close.initiated_close();
    first_resumed = true;
    auto after_close = uv::detail::close_completion(*pair.client);
    co_await after_close;
    post_close_joined = !after_close.initiated_close();
    pair.loop.stop();
  };
  auto second = [&]() -> uv::co::task<void> {
    auto close = uv::detail::close_completion(*pair.client);
    co_await close;
    second_initiated = close.initiated_close();
    second_resumed = true;
  };

  auto wrong_execution = uv::co::spawn(other_loop, wrong_loop());
  EXPECT_TRUE(wrong_execution.done());
  EXPECT_NO_THROW(wrong_execution.rethrow_if_failed());
  EXPECT_TRUE(wrong_loop_rejected);
  other_loop.close();

  auto first_execution = uv::co::spawn(pair.loop, first());
  auto second_execution = uv::co::spawn(pair.loop, second());
  pair.loop.run();

  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  EXPECT_TRUE(first_initiated);
  EXPECT_FALSE(second_initiated);
  EXPECT_TRUE(first_resumed);
  EXPECT_TRUE(second_resumed);
  EXPECT_TRUE(post_close_joined);
  pair.close();
}
