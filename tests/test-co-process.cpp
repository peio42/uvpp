#include "co-test-support.hpp"

#include <csignal>
#include <optional>

#include "uvpp/handles/process.hpp"

namespace {

uv::process_options shell(std::string command) {
  uv::process_options options;
  options.file = "/bin/sh";
  options.arguments = {"-c", std::move(command)};
  return options;
}

} // namespace

TEST(UvppV3Process, spawnsSynchronouslyWaitsAndCloses) {
  uv::loop loop;
  uv::process child(loop, shell("exit 7"));
  std::optional<uv::process_exit> exit;

  auto wait = [&]() -> uv::co::task<void> {
    exit.emplace(co_await child.wait());
    co_await child.close();
  };

  auto execution = uv::co::spawn(loop, wait());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->status, 7);
  EXPECT_EQ(exit->signal, 0);
  EXPECT_TRUE(child.closing());
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Process, remembersExitBeforeWait) {
  uv::loop loop;
  uv::process child(loop, shell("exit 5"));
  loop.run();
  ASSERT_TRUE(child.exited());

  std::optional<uv::process_exit> exit;
  auto wait = [&]() -> uv::co::task<void> {
    exit.emplace(co_await child.wait());
    co_await child.close();
  };
  auto execution = uv::co::spawn(loop, wait());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->status, 5);
  EXPECT_EQ(exit->signal, 0);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Process, passesArgumentsAndOptionalWorkingDirectory) {
  uv::loop loop;
  auto options = shell("test \"$1\" = value && test \"$PWD\" = /tmp");
  options.arguments.push_back("process-test");
  options.arguments.push_back("value");
  options.cwd = "/tmp";
  uv::process child(loop, options);
  std::optional<uv::process_exit> exit;

  auto wait = [&]() -> uv::co::task<void> {
    exit.emplace(co_await child.wait());
    co_await child.close();
  };
  auto execution = uv::co::spawn(loop, wait());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->status, 0);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Process, rejectsASecondConcurrentWaiter) {
  uv::loop loop;
  uv::process child(loop, shell("sleep 0.02; exit 0"));
  std::optional<uv::process_exit> first;
  std::optional<uv::result<uv::process_exit>> second;

  auto first_wait = [&]() -> uv::co::task<void> {
    first.emplace(co_await child.wait());
    co_await child.close();
  };
  auto second_wait = [&]() -> uv::co::task<void> {
    second.emplace(co_await uv::ops::wait(child));
  };

  auto first_execution = uv::co::spawn(loop, first_wait());
  auto second_execution = uv::co::spawn(loop, second_wait());
  loop.run();

  EXPECT_TRUE(first_execution.done());
  EXPECT_TRUE(second_execution.done());
  EXPECT_NO_THROW(first_execution.rethrow_if_failed());
  EXPECT_NO_THROW(second_execution.rethrow_if_failed());
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_FALSE(*second);
  EXPECT_EQ(second->error(), uv::make_error_code(UV_EBUSY));
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Process, cancellingWaitLeavesTheProcessWaitable) {
  uv::loop loop;
  uv::process child(loop, shell("sleep 0.02; exit 3"));
  std::optional<uv::result<uv::process_exit>> cancelled;

  auto wait_then_cancel = [&]() -> uv::co::task<void> {
    cancelled.emplace(co_await uv::ops::wait(child));
  };
  auto cancelled_execution = uv::co::spawn(loop, wait_then_cancel());
  cancelled_execution.request_stop();

  ASSERT_TRUE(cancelled_execution.done());
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_FALSE(*cancelled);
  EXPECT_EQ(cancelled->error(), uv::make_error_code(UV_ECANCELED));

  std::optional<uv::process_exit> exit;
  auto wait_again = [&]() -> uv::co::task<void> {
    exit.emplace(co_await child.wait());
    co_await child.close();
  };
  auto execution = uv::co::spawn(loop, wait_again());
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->status, 3);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Process, killReportsTerminationThroughWait) {
  uv::loop loop;
  uv::process child(loop, shell("sleep 10"));
  std::optional<uv::process_exit> exit;

  auto wait = [&]() -> uv::co::task<void> {
    exit.emplace(co_await child.wait());
    co_await child.close();
  };
  auto execution = uv::co::spawn(loop, wait());
  EXPECT_NO_THROW(child.kill(SIGTERM));
  loop.run();

  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(exit.has_value());
  EXPECT_EQ(exit->signal, SIGTERM);
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Process, closeBeforeExitReportsBusy) {
  uv::loop loop;
  uv::process child(loop, shell("sleep 10"));
  std::optional<uv::status> close_status;

  auto attempt_close = [&]() -> uv::co::task<void> {
    close_status.emplace(co_await uv::ops::close(child));
  };
  auto execution = uv::co::spawn(loop, attempt_close());
  ASSERT_TRUE(execution.done());
  ASSERT_TRUE(close_status.has_value());
  EXPECT_FALSE(*close_status);
  EXPECT_EQ(close_status->error(), uv::make_error_code(UV_EBUSY));

  ASSERT_TRUE(uv::ops::kill(child, SIGTERM));
  loop.run();
  ASSERT_TRUE(child.exited());
  child.request_close();
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Process, failedSpawnRetainsTheNativeHandleThroughClose) {
  uv::loop loop;
  EXPECT_THROW((uv::process{loop, uv::process_options{.file = "/nonexistent/uvpp-process"}}), uv::error);
  EXPECT_EQ(loop.try_close(), uv::make_error_code(UV_EBUSY));
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3Process, destructionBeforeExitDefersCloseUntilExit) {
  uv::loop loop;
  {
    uv::process child(loop, shell("sleep 0.02"));
  }
  loop.run();
  EXPECT_NO_THROW(loop.close());
}
