#include <csignal>
#include <cstdlib>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Signal, runsRuntimeCallback) {
  uv::loop loop;
  uv::signal signal(loop);

  int marker = 42;
  int called = 0;

  signal.user_data(marker);
  signal.start_oneshot(SIGUSR1, [&](uv::signal &self, int signum) {
    called++;
    EXPECT_EQ(signum, SIGUSR1);
    EXPECT_EQ(self.user_data<int>(), &marker);
    self.close();
  });

  ASSERT_EQ(::raise(SIGUSR1), 0);
  loop.run();

  EXPECT_EQ(called, 1);
  loop.close();
}

namespace {

int static_signal_called = 0;

void on_static_signal(uv::signal &signal, int signum) {
  static_signal_called++;
  EXPECT_EQ(signum, SIGUSR1);
  signal.close();
}

}

TEST(Uvpp2Signal, runsStaticCallback) {
  uv::loop loop;
  uv::signal signal(loop);
  static_signal_called = 0;

  signal.start_oneshot_static<on_static_signal>(SIGUSR1);

  ASSERT_EQ(::raise(SIGUSR1), 0);
  loop.run();

  EXPECT_EQ(static_signal_called, 1);
  loop.close();
}

TEST(Uvpp2Poll, reportsReadableFileDescriptor) {
  int fds[2]{};
  ASSERT_EQ(::pipe(fds), 0);

  uv::loop loop;
  uv::poll poll(loop, fds[0]);

  int called = 0;
  poll.start(uv::poll_event::readable, [&](uv::poll &self, uv::result status, int events) {
    called++;
    EXPECT_TRUE(status);
    EXPECT_TRUE(uv::has_poll_event(events, uv::poll_event::readable));

    char byte = 0;
    EXPECT_EQ(::read(fds[0], &byte, 1), 1);
    EXPECT_EQ(byte, 'x');
    self.close();
  });

  char byte = 'x';
  ASSERT_EQ(::write(fds[1], &byte, 1), 1);

  loop.run();
  EXPECT_EQ(called, 1);
  loop.close();

  close(fds[0]);
  close(fds[1]);
}

namespace {

uv::poll *static_poll_handle = nullptr;
int static_poll_read_fd = -1;
int static_poll_called = 0;

void on_static_poll(uv::poll &, uv::result status, int events) {
  static_poll_called++;
  EXPECT_TRUE(status);
  EXPECT_TRUE(uv::has_poll_event(events, uv::poll_event::readable));

  char byte = 0;
  EXPECT_EQ(::read(static_poll_read_fd, &byte, 1), 1);
  EXPECT_EQ(byte, 'y');
  static_poll_handle->close();
}

}

TEST(Uvpp2Poll, reportsReadableFileDescriptorWithStaticCallback) {
  int fds[2]{};
  ASSERT_EQ(::pipe(fds), 0);

  uv::loop loop;
  uv::poll poll(loop, fds[0]);
  static_poll_handle = &poll;
  static_poll_read_fd = fds[0];
  static_poll_called = 0;

  poll.start_static<on_static_poll>(uv::poll_event::readable);

  char byte = 'y';
  ASSERT_EQ(::write(fds[1], &byte, 1), 1);

  loop.run();
  EXPECT_EQ(static_poll_called, 1);
  loop.close();

  static_poll_handle = nullptr;
  static_poll_read_fd = -1;
  close(fds[0]);
  close(fds[1]);
}

TEST(Uvpp2ProcessOptions, buildsFluentOptions) {
  auto options = uv::process_options::make("git")
    .arg("status")
    .args({"--short", "--branch"})
    .cwd("/tmp/repo")
    .env("LANG", "C")
    .inherit_stdout()
    .inherit_stderr()
    .detached()
    .windows_hide();

  EXPECT_EQ(options.file, "git");
  ASSERT_EQ(options.arguments.size(), 3);
  EXPECT_EQ(options.arguments[0], "status");
  EXPECT_EQ(options.arguments[1], "--short");
  EXPECT_EQ(options.arguments[2], "--branch");
  EXPECT_EQ(options.working_directory, "/tmp/repo");
  ASSERT_EQ(options.environment.size(), 1);
  EXPECT_EQ(options.environment[0], "LANG=C");
  EXPECT_FALSE(options.inherit_parent_environment);
  EXPECT_TRUE((options.flags & UV_PROCESS_DETACHED) != 0);
  EXPECT_TRUE((options.flags & UV_PROCESS_WINDOWS_HIDE) != 0);

  ASSERT_EQ(options.stdio_entries.size(), 3);
  EXPECT_EQ(options.stdio_entries[0].flags, UV_IGNORE);
  EXPECT_EQ(options.stdio_entries[1].flags, UV_INHERIT_FD);
  EXPECT_EQ(options.stdio_entries[1].data.fd, 1);
  EXPECT_EQ(options.stdio_entries[2].flags, UV_INHERIT_FD);
  EXPECT_EQ(options.stdio_entries[2].data.fd, 2);
}

TEST(Uvpp2ProcessOptions, acceptsExplicitStdioEntries) {
  auto entries = {
    uv::process_stdio::ignore(),
    uv::process_stdio::inherit_fd(1),
    uv::process_stdio::inherit_fd(2),
  };

  auto options = uv::process_options::make("tool").stdio(entries);

  ASSERT_EQ(options.stdio_entries.size(), 3);
  EXPECT_EQ(options.stdio_entries[0].flags, UV_IGNORE);
  EXPECT_EQ(options.stdio_entries[1].flags, UV_INHERIT_FD);
  EXPECT_EQ(options.stdio_entries[1].data.fd, 1);
  EXPECT_EQ(options.stdio_entries[2].flags, UV_INHERIT_FD);
  EXPECT_EQ(options.stdio_entries[2].data.fd, 2);
}

TEST(Uvpp2ProcessOptions, buildsPipeStdioEntries) {
  uv::loop loop;
  uv::pipe output(loop);

  auto options = uv::process_options::make("tool")
    .empty_environment()
    .pipe_stdout(output);

  EXPECT_FALSE(options.inherit_parent_environment);
  EXPECT_TRUE(options.environment.empty());
  ASSERT_EQ(options.stdio_entries.size(), 2);
  EXPECT_EQ(options.stdio_entries[0].flags, UV_IGNORE);
  EXPECT_EQ(options.stdio_entries[1].flags, static_cast<uv_stdio_flags>(UV_CREATE_PIPE | UV_WRITABLE_PIPE));
  EXPECT_EQ(options.stdio_entries[1].data.stream, output.native_stream());

  output.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Process, inheritedEnvironmentIsVisibleToChild) {
  ::setenv("UVPP_TEST_INHERIT_VAR", "present", 1);

  uv::loop loop;
  auto options = uv::process_options::make("/bin/sh")
    .args({"-c", "test -n \"$UVPP_TEST_INHERIT_VAR\""});

  int exit_status = -1;
  uv::process process(loop, options, [&](uv::process &self, uv::process_exit exit) {
    exit_status = static_cast<int>(exit.status);
    self.close();
  });

  loop.run();
  EXPECT_EQ(exit_status, 0);
  loop.close();

  ::unsetenv("UVPP_TEST_INHERIT_VAR");
}

TEST(Uvpp2Process, emptyEnvironmentHidesParentVariables) {
  ::setenv("UVPP_TEST_INHERIT_VAR", "present", 1);

  uv::loop loop;
  auto options = uv::process_options::make("/bin/sh")
    .args({"-c", "test -z \"$UVPP_TEST_INHERIT_VAR\""})
    .empty_environment();

  int exit_status = -1;
  uv::process process(loop, options, [&](uv::process &self, uv::process_exit exit) {
    exit_status = static_cast<int>(exit.status);
    self.close();
  });

  loop.run();
  EXPECT_EQ(exit_status, 0);
  loop.close();

  ::unsetenv("UVPP_TEST_INHERIT_VAR");
}

TEST(Uvpp2Process, reportsExitStatus) {
  uv::loop loop;

  auto options = uv::process_options::make("/bin/sh")
    .args({"-c", "exit 7"});

  bool exited = false;
  bool closed = false;

  uv::process process(loop, options, [&](uv::process &self, uv::process_exit exit) {
    exited = true;
    EXPECT_EQ(exit.status, 7);
    EXPECT_EQ(exit.signal, 0);
    EXPECT_GT(self.pid(), 0);
    EXPECT_EQ(&self, &uv::process::from_native(self.native()));
    self.close([&](uv::process &) {
      closed = true;
    });
  });

  loop.run();

  EXPECT_TRUE(exited);
  EXPECT_TRUE(closed);
  loop.close();
}

namespace {

bool static_process_exited = false;
bool static_process_closed = false;

void on_static_process_exit(uv::process &process, uv::process_exit exit) {
  static_process_exited = true;
  EXPECT_EQ(exit.status, 3);
  EXPECT_EQ(exit.signal, 0);
  process.close([](uv::process &) {
    static_process_closed = true;
  });
}

}

TEST(Uvpp2Process, reportsExitStatusWithStaticCallback) {
  uv::loop loop;

  auto options = uv::process_options::make("/bin/sh")
    .args({"-c", "exit 3"});
  static_process_exited = false;
  static_process_closed = false;

  uv::process process(loop, options, uv::process::static_callback<on_static_process_exit>{});

  loop.run();

  EXPECT_TRUE(static_process_exited);
  EXPECT_TRUE(static_process_closed);
  loop.close();
}
