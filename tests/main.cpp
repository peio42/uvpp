#include "gtest/gtest.h"
#include "platform-support.hpp"

int main(int argc, char **argv) {
  if (const int result = uvpp::test::child_main(argc, argv); result >= 0) return result;
  // libuv and sanitizers may have started background threads before a death
  // test. Re-exec its body instead of running user code in a fork-only child.
  // Set the default before InitGoogleTest so explicit command-line flags win.
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

