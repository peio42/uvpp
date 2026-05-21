#include <optional>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2Tty, initializesWhenStdoutIsATty) {
  if (uv_guess_handle(STDOUT_FILENO) != UV_TTY) {
    GTEST_SKIP() << "stdout is not a tty";
  }

  uv::loop loop;
  uv::tty tty(loop, STDOUT_FILENO, false);

  auto size = tty.size();
  EXPECT_GE(size.width, 0);
  EXPECT_GE(size.height, 0);

  tty.close();
  loop.run();
  loop.close();
}

#if UVPP_HAS_TTY_VTERM_STATE
TEST(Uvpp2Tty, exposesVirtualTerminalState) {
  std::optional<uv::tty_vterm_state> prior;
  try {
    prior = uv::tty::vterm_state();
  } catch (const uv::error &) {}

  uv::tty::set_vterm_state(uv::tty_vterm_state::supported);

  try {
    auto state = uv::tty::vterm_state();
    EXPECT_TRUE(state == uv::tty_vterm_state::supported ||
                state == uv::tty_vterm_state::unsupported);
  } catch (const uv::error &error) {
    EXPECT_EQ(error.code(), uv::make_error_code(UV_ENOTSUP));
  }

  if (prior)
    uv::tty::set_vterm_state(*prior);
}
#endif
