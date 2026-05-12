#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Tty, initializesWhenStdoutIsATty) {
  if (uv_guess_handle(STDOUT_FILENO) != UV_TTY) {
    GTEST_SKIP() << "stdout is not a tty";
  }

  uvpp::loop loop;
  uvpp::tty tty(loop, STDOUT_FILENO, false);

  auto size = tty.size();
  EXPECT_GE(size.width, 0);
  EXPECT_GE(size.height, 0);

  tty.close();
  loop.run();
  loop.close();
}
