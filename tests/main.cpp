#include "gtest/gtest.h"
#include "platform-support.hpp"

int main(int argc, char **argv) {
  if (const int result = uvpp::test::child_main(argc, argv); result >= 0) return result;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

