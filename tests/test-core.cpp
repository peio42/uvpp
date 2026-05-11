#include <system_error>
#include <memory>

#include <uv.h>

#include "gtest/gtest.h"
#include "uvpp/core/callback.hpp"
#include "uvpp/uv.hpp"

namespace {

void leak_opendir_result_for_death_test() {
  uvpp::fs::raw::opendir_result result{0, reinterpret_cast<void *>(1)};
}

}

TEST(Uvpp2Core, mapsLibuvStatusesToErrorCodes) {
  auto success = uvpp::make_error_code(0);
  EXPECT_FALSE(success);

  auto failure = uvpp::make_error_code(UV_EINVAL);
  EXPECT_TRUE(failure);
  EXPECT_EQ(failure.value(), UV_EINVAL);
  EXPECT_EQ(&failure.category(), &uvpp::category());
  EXPECT_FALSE(failure.message().empty());
}

TEST(Uvpp2Core, checkThrowsUvppErrorOnLibuvFailure) {
  EXPECT_EQ(uvpp::throw_if_error(0), 0);

  try {
    uvpp::throw_if_error(UV_EINVAL);
    FAIL() << "uvpp::check should throw on negative libuv status";
  } catch (const uvpp::error &err) {
    EXPECT_EQ(err.code(), uvpp::make_error_code(UV_EINVAL));
  }
}

TEST(Uvpp2Core, resultUsesOneErrorGrammar) {
  uvpp::result ok;
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ok.ok());
  EXPECT_EQ(ok.status(), 0);
  EXPECT_FALSE(ok.error_code());

  uvpp::result failed{UV_ECONNREFUSED};
  EXPECT_FALSE(failed);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.status(), UV_ECONNREFUSED);
  EXPECT_EQ(failed.error_code(), uvpp::make_error_code(UV_ECONNREFUSED));
}

TEST(Uvpp2Core, loopCloseIsExplicitAndReportsBusyLoops) {
  uvpp::loop loop;
  uvpp::timer timer(loop);

  auto busy = loop.try_close();
  ASSERT_TRUE(busy);
  EXPECT_EQ(busy, uvpp::make_error_code(UV_EBUSY));
  EXPECT_THROW(loop.close(), uvpp::error);

  timer.close();
  loop.run();
  EXPECT_FALSE(loop.try_close());
}

TEST(Uvpp2Core, requestCallbacksAreOneShot) {
  {
    uvpp::write_request request;
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    bool called = false;

    request.set_callback([token, &called](uvpp::write_request&, uvpp::result) {
      called = true;
    });
    token.reset();
    EXPECT_FALSE(weak.expired());

    request.invoke(0);
    EXPECT_TRUE(called);
    EXPECT_TRUE(weak.expired());
  }

  {
    uvpp::connect_request request;
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    bool called = false;

    request.set_callback([token, &called](uvpp::connect_request&, uvpp::result) {
      called = true;
    });
    token.reset();

    request.invoke(0);
    EXPECT_TRUE(called);
    EXPECT_TRUE(weak.expired());
  }

  {
    uvpp::shutdown_request request;
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    bool called = false;

    request.set_callback([token, &called](uvpp::shutdown_request&, uvpp::result) {
      called = true;
    });
    token.reset();

    request.invoke(0);
    EXPECT_TRUE(called);
    EXPECT_TRUE(weak.expired());
  }

  {
    uvpp::udp_send_request request;
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    bool called = false;

    request.set_callback([token, &called](uvpp::udp_send_request&, uvpp::result) {
      called = true;
    });
    token.reset();

    request.invoke(0);
    EXPECT_TRUE(called);
    EXPECT_TRUE(weak.expired());
  }
}

#if GTEST_HAS_DEATH_TEST
TEST(Uvpp2CoreDeathTest, callbackExceptionsTerminate) {
  EXPECT_DEATH(uvpp::detail::invoke_callback([] {
    throw 1;
  }), ".*");
}

TEST(Uvpp2CoreDeathTest, successfulOpendirResultMustBeConsumed) {
  EXPECT_DEATH(leak_opendir_result_for_death_test(), ".*");
}
#endif
