#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

class ErrorTest : public testing::Test {
protected:
  // uv::Loop *loop = uv::Loop::getDefault();
};


TEST_F(ErrorTest, errorMessages) {
  uv::Error error{-1};
  ASSERT_EQ(error.code, -1);
  ASSERT_STREQ(error.name(), "EPERM");
  ASSERT_STREQ(error.message(), "operation not permitted");
}