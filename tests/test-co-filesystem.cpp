#include "co-test-support.hpp"

#include <array>
#include <optional>
#include <span>
#include <fcntl.h>

#include "uvpp/co/resource_scope.hpp"
#include "uvpp/co/task_scope.hpp"
#include "uvpp/fs/coroutines.hpp"

using namespace std::chrono_literals;
using namespace uvpp::test;

TEST(UvppV3Filesystem, openWriteReadCloseUsesOneStableOwner) {
  const auto path = std::filesystem::temp_directory_path() /
      ("uvpp-v3-filesystem-" + std::to_string(::getpid()) + "-roundtrip");
  std::filesystem::remove(path);
  uv::loop loop;
  std::array<std::byte, 5> input{
      std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}};
  std::array<std::byte, 5> output{};
  std::optional<uv::fs::file_read_result> read_result;

  auto roundtrip = [&]() -> uv::co::task<void> {
    auto opened = co_await uv::fs::open(path.string(), O_CREAT | O_TRUNC | O_RDWR, 0600);
    EXPECT_TRUE(opened.is_open());
    EXPECT_EQ(co_await uv::fs::write(opened, std::span<const std::byte>{input}, 0), input.size());
    read_result.emplace(co_await uv::fs::read(opened, std::span<std::byte>{output}, 0));
    co_await uv::fs::close(opened);
    EXPECT_FALSE(opened.is_open());
  };

  auto execution = uv::co::spawn(loop, roundtrip());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(read_result.has_value());
  EXPECT_EQ(read_result->count(), output.size());
  EXPECT_FALSE(read_result->eof());
  EXPECT_EQ(output, input);
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, releasesReadSlotBeforeResumingContinuation) {
  const auto path = filesystem_test_path("sequential-reads");
  std::filesystem::remove(path);
  write_filesystem_test_file(path, "firstsecond");

  uv::loop loop;
  std::array<std::byte, 5> first_buffer{};
  std::array<std::byte, 6> second_buffer{};

  auto scenario = [&]() -> uv::co::task<void> {
    auto file = co_await uv::fs::open(path.string(), O_RDONLY, 0);
    const auto first = co_await uv::fs::read(file, first_buffer, 0);
    EXPECT_EQ(first.count(), first_buffer.size());
    const auto second = co_await uv::fs::read(file, second_buffer, 5);
    EXPECT_EQ(second.count(), second_buffer.size());
    co_await uv::fs::close(file);
  };

  auto execution = uv::co::spawn(loop, scenario());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, opsReturnsOpenAndWriteFailuresWithoutThrowing) {
  const auto path = std::filesystem::temp_directory_path() /
      ("uvpp-v3-filesystem-" + std::to_string(::getpid()) + "-read-only");
  std::filesystem::remove(path);
  {
    std::ofstream created{path};
    created << "x";
  }
  uv::loop loop;
  std::optional<uv::result<uv::fs::file>> missing;
  std::optional<uv::result<std::size_t>> write_result;
  std::optional<uv::status> close_result;
  const std::array<std::byte, 1> data{std::byte{'x'}};

  auto operations = [&]() -> uv::co::task<void> {
    missing.emplace(co_await uv::ops::fs::open("/uvpp-definitely-missing-parent/file", O_RDONLY, 0));
    auto opened = co_await uv::ops::fs::open(path.string(), O_RDONLY, 0);
    EXPECT_TRUE(opened);
    if (!opened) {
      co_return;
    }
    auto file = std::move(opened).value();
    write_result.emplace(co_await uv::ops::fs::write(file, std::span<const std::byte>{data}, 0));
    close_result.emplace(co_await uv::ops::fs::close(file));
  };

  auto execution = uv::co::spawn(loop, operations());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(missing.has_value());
  EXPECT_FALSE(*missing);
  ASSERT_TRUE(write_result.has_value());
  EXPECT_FALSE(*write_result);
  ASSERT_TRUE(close_result.has_value());
  EXPECT_TRUE(*close_result);
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, resourceScopeClosesAnAdoptedFile) {
  const auto path = std::filesystem::temp_directory_path() /
      ("uvpp-v3-filesystem-" + std::to_string(::getpid()) + "-scope");
  std::filesystem::remove(path);
  uv::loop loop;
  const std::array<std::byte, 1> data{std::byte{'s'}};

  auto scoped = [&]() -> uv::co::task<void> {
    uv::co::resource_scope resources{loop};
    auto opened = co_await uv::fs::open(path.string(), O_CREAT | O_TRUNC | O_RDWR, 0600);
    auto registered = resources.own(std::move(opened));
    EXPECT_EQ(co_await uv::fs::write(registered.view(), std::span<const std::byte>{data}, 0), 1u);
    co_await resources.finish();
  };

  auto execution = uv::co::spawn(loop, scoped());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, concurrentCloseAwaitersJoinOneTerminalRequest) {
  const auto path = std::filesystem::temp_directory_path() /
      ("uvpp-v3-filesystem-" + std::to_string(::getpid()) + "-close-join");
  std::filesystem::remove(path);
  uv::loop loop;
  bool first_closed = false;
  bool second_closed = false;

  auto close_twice = [&]() -> uv::co::task<void> {
    auto opened = co_await uv::fs::open(path.string(), O_CREAT | O_TRUNC | O_RDWR, 0600);
    uv::co::task_scope closers{loop};
    auto first = [&]() -> uv::co::task<void> {
      co_await uv::fs::close(opened);
      first_closed = true;
    };
    auto second = [&]() -> uv::co::task<void> {
      co_await uv::fs::close(opened);
      second_closed = true;
    };
    closers.spawn(first());
    closers.spawn(second());
    co_await closers.join();
    EXPECT_FALSE(opened.is_open());
  };

  auto execution = uv::co::spawn(loop, close_twice());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(first_closed);
  EXPECT_TRUE(second_closed);
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, rejectsSimultaneousReads) {
  const auto path = filesystem_test_path("read-conflict");
  std::filesystem::remove(path);
  write_filesystem_test_file(path, "read");

  uv::loop loop;
  std::array<std::byte, 4> first_buffer{};
  std::array<std::byte, 4> second_buffer{};
  bool first_started = false;
  bool first_completed = false;
  int second_status = 0;

  auto scenario = [&]() -> uv::co::task<void> {
    auto file = co_await uv::fs::open(path.string(), O_RDONLY, 0);
    uv::co::task_scope reads{loop};
    auto first = [&]() -> uv::co::task<void> {
      first_started = true;
      const auto result = co_await uv::ops::fs::read(file, first_buffer, 0);
      EXPECT_TRUE(result);
      first_completed = true;
    };

    reads.spawn(first());
    EXPECT_TRUE(first_started);
    const auto second = co_await uv::ops::fs::read(file, second_buffer, 0);
    EXPECT_FALSE(second);
    second_status = second.error().native();
    co_await reads.join();
    co_await uv::fs::close(file);
  };

  auto execution = uv::co::spawn(loop, scenario());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(second_status, UV_EBUSY);
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, rejectsSimultaneousWrites) {
  const auto path = filesystem_test_path("write-conflict");
  std::filesystem::remove(path);

  uv::loop loop;
  const std::array<std::byte, 5> first_data{
      std::byte{'f'}, std::byte{'i'}, std::byte{'r'}, std::byte{'s'}, std::byte{'t'}};
  const std::array<std::byte, 6> second_data{
      std::byte{'s'}, std::byte{'e'}, std::byte{'c'}, std::byte{'o'}, std::byte{'n'}, std::byte{'d'}};
  bool first_started = false;
  bool first_completed = false;
  int second_status = 0;

  auto scenario = [&]() -> uv::co::task<void> {
    auto file = co_await uv::fs::open(path.string(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    uv::co::task_scope writes{loop};
    auto first = [&]() -> uv::co::task<void> {
      first_started = true;
      const auto result = co_await uv::ops::fs::write(file, first_data, 0);
      EXPECT_TRUE(result);
      first_completed = true;
    };

    writes.spawn(first());
    EXPECT_TRUE(first_started);
    const auto second = co_await uv::ops::fs::write(file, second_data, 0);
    EXPECT_FALSE(second);
    second_status = second.error().native();
    co_await writes.join();
    co_await uv::fs::close(file);
  };

  auto execution = uv::co::spawn(loop, scenario());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(first_completed);
  EXPECT_EQ(second_status, UV_EBUSY);
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, permitsOneReadAndOneWrite) {
  const auto path = filesystem_test_path("duplex");
  std::filesystem::remove(path);
  write_filesystem_test_file(path, "abcdefgh");

  uv::loop loop;
  std::array<std::byte, 4> read_buffer{};
  const std::array<std::byte, 4> write_buffer{
      std::byte{'W'}, std::byte{'X'}, std::byte{'Y'}, std::byte{'Z'}};
  bool read_completed = false;
  bool write_completed = false;

  auto scenario = [&]() -> uv::co::task<void> {
    auto file = co_await uv::fs::open(path.string(), O_RDWR, 0);
    uv::co::task_scope operations{loop};
    auto reader = [&]() -> uv::co::task<void> {
      const auto result = co_await uv::ops::fs::read(file, read_buffer, 0);
      EXPECT_TRUE(result);
      read_completed = true;
    };
    auto writer = [&]() -> uv::co::task<void> {
      const auto result = co_await uv::ops::fs::write(file, write_buffer, 4);
      EXPECT_TRUE(result);
      write_completed = true;
    };

    operations.spawn(reader());
    operations.spawn(writer());
    co_await operations.join();
    co_await uv::fs::close(file);
  };

  auto execution = uv::co::spawn(loop, scenario());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(read_completed);
  EXPECT_TRUE(write_completed);
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, closeRejectsActiveReadWithoutChangingIt) {
  const auto path = filesystem_test_path("close-read-conflict");
  std::filesystem::remove(path);
  write_filesystem_test_file(path, "read");

  uv::loop loop;
  std::array<std::byte, 4> buffer{};
  bool read_completed = false;
  bool close_rejected = false;
  bool final_close_completed = false;

  auto scenario = [&]() -> uv::co::task<void> {
    auto file = co_await uv::fs::open(path.string(), O_RDONLY, 0);
    uv::co::task_scope reads{loop};
    auto reader = [&]() -> uv::co::task<void> {
      const auto result = co_await uv::ops::fs::read(file, buffer, 0);
      EXPECT_TRUE(result);
      read_completed = true;
    };

    reads.spawn(reader());
    const auto rejected = co_await uv::ops::fs::close(file);
    close_rejected = rejected.error().native() == UV_EBUSY;
    EXPECT_TRUE(file.is_open());
    co_await reads.join();
    co_await uv::fs::close(file);
    final_close_completed = true;
  };

  auto execution = uv::co::spawn(loop, scenario());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(close_rejected);
  EXPECT_TRUE(read_completed);
  EXPECT_TRUE(final_close_completed);
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, closeRejectsActiveWriteWithoutChangingIt) {
  const auto path = filesystem_test_path("close-write-conflict");
  std::filesystem::remove(path);

  uv::loop loop;
  const std::array<std::byte, 5> buffer{
      std::byte{'w'}, std::byte{'r'}, std::byte{'i'}, std::byte{'t'}, std::byte{'e'}};
  bool write_completed = false;
  bool close_rejected = false;
  bool final_close_completed = false;

  auto scenario = [&]() -> uv::co::task<void> {
    auto file = co_await uv::fs::open(path.string(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    uv::co::task_scope writes{loop};
    auto writer = [&]() -> uv::co::task<void> {
      const auto result = co_await uv::ops::fs::write(file, buffer, 0);
      EXPECT_TRUE(result);
      write_completed = true;
    };

    writes.spawn(writer());
    const auto rejected = co_await uv::ops::fs::close(file);
    close_rejected = rejected.error().native() == UV_EBUSY;
    EXPECT_TRUE(file.is_open());
    co_await writes.join();
    co_await uv::fs::close(file);
    final_close_completed = true;
  };

  auto execution = uv::co::spawn(loop, scenario());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(close_rejected);
  EXPECT_TRUE(write_completed);
  EXPECT_TRUE(final_close_completed);
  loop.close();
  std::filesystem::remove(path);
}

TEST(UvppV3Filesystem, stopWaitsForSubmittedBorrowedWriteCompletion) {
  const auto path = filesystem_test_path("write-cancellation");
  std::filesystem::remove(path);

  uv::loop loop;
  bool write_started = false;
  bool write_completed = false;
  bool buffer_was_retained = false;
  bool stop_observed = false;
  bool pending_after_stop = false;
  bool close_rejected = false;
  bool final_close_completed = false;

  auto scenario = [&]() -> uv::co::task<void> {
    auto file = co_await uv::fs::open(path.string(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    uv::co::task_scope writes{loop};
    auto writer = [&]() -> uv::co::task<void> {
      std::array<std::byte, 7> buffer{
          std::byte{'b'}, std::byte{'o'}, std::byte{'r'}, std::byte{'r'},
          std::byte{'o'}, std::byte{'w'}, std::byte{'e'}};
      write_started = true;
      (void)co_await uv::ops::fs::write(file, buffer, 0);
      buffer_was_retained = buffer.front() == std::byte{'b'};
      write_completed = true;
      stop_observed = co_await uv::co::stop_requested();
    };

    writes.spawn(writer());
    EXPECT_TRUE(write_started);
    writes.request_stop();
    pending_after_stop = !write_completed;
    const auto rejected = co_await uv::ops::fs::close(file);
    close_rejected = rejected.error().native() == UV_EBUSY;
    co_await writes.join();
    co_await uv::fs::close(file);
    final_close_completed = true;
  };

  auto execution = uv::co::spawn(loop, scenario());
  loop.run();

  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_TRUE(pending_after_stop);
  EXPECT_TRUE(close_rejected);
  EXPECT_TRUE(write_completed);
  EXPECT_TRUE(buffer_was_retained);
  EXPECT_TRUE(stop_observed);
  EXPECT_TRUE(final_close_completed);
  loop.close();
  std::filesystem::remove(path);
}
