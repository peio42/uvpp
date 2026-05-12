#include <csignal>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Signal, runsRuntimeCallback) {
  uvpp::loop loop;
  uvpp::signal signal(loop);

  int marker = 42;
  int called = 0;

  signal.user_data(marker);
  signal.start_oneshot(SIGUSR1, [&](uvpp::signal &self, int signum) {
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

void on_static_signal(uvpp::signal &signal, int signum) {
  static_signal_called++;
  EXPECT_EQ(signum, SIGUSR1);
  signal.close();
}

}

TEST(Uvpp2Signal, runsStaticCallback) {
  uvpp::loop loop;
  uvpp::signal signal(loop);
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

  uvpp::loop loop;
  uvpp::poll poll(loop, fds[0]);

  int called = 0;
  poll.start(uvpp::poll_event::readable, [&](uvpp::poll &self, uvpp::result status, int events) {
    called++;
    EXPECT_TRUE(status);
    EXPECT_TRUE(uvpp::has_poll_event(events, uvpp::poll_event::readable));

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

uvpp::poll *static_poll_handle = nullptr;
int static_poll_read_fd = -1;
int static_poll_called = 0;

void on_static_poll(uvpp::poll &, uvpp::result status, int events) {
  static_poll_called++;
  EXPECT_TRUE(status);
  EXPECT_TRUE(uvpp::has_poll_event(events, uvpp::poll_event::readable));

  char byte = 0;
  EXPECT_EQ(::read(static_poll_read_fd, &byte, 1), 1);
  EXPECT_EQ(byte, 'y');
  static_poll_handle->close();
}

}

TEST(Uvpp2Poll, reportsReadableFileDescriptorWithStaticCallback) {
  int fds[2]{};
  ASSERT_EQ(::pipe(fds), 0);

  uvpp::loop loop;
  uvpp::poll poll(loop, fds[0]);
  static_poll_handle = &poll;
  static_poll_read_fd = fds[0];
  static_poll_called = 0;

  poll.start_static<on_static_poll>(uvpp::poll_event::readable);

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

TEST(Uvpp2Process, reportsExitStatus) {
  uvpp::loop loop;

  uvpp::process_options options;
  options.file = "/bin/sh";
  options.arguments = {"-c", "exit 7"};

  bool exited = false;
  bool closed = false;

  uvpp::process process(loop, options, [&](uvpp::process &self, uvpp::process_exit exit) {
    exited = true;
    EXPECT_EQ(exit.status, 7);
    EXPECT_EQ(exit.signal, 0);
    EXPECT_GT(self.pid(), 0);
    EXPECT_EQ(&self, &uvpp::process::from_native(self.native()));
    self.close([&](uvpp::process &) {
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

void on_static_process_exit(uvpp::process &process, uvpp::process_exit exit) {
  static_process_exited = true;
  EXPECT_EQ(exit.status, 3);
  EXPECT_EQ(exit.signal, 0);
  process.close([](uvpp::process &) {
    static_process_closed = true;
  });
}

}

TEST(Uvpp2Process, reportsExitStatusWithStaticCallback) {
  uvpp::loop loop;

  uvpp::process_options options;
  options.file = "/bin/sh";
  options.arguments = {"-c", "exit 3"};
  static_process_exited = false;
  static_process_closed = false;

  uvpp::process process(loop, options, uvpp::process::static_callback<on_static_process_exit>{});

  loop.run();

  EXPECT_TRUE(static_process_exited);
  EXPECT_TRUE(static_process_closed);
  loop.close();
}
