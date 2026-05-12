#include <system_error>
#include <memory>

#include <uv.h>

#include "gtest/gtest.h"
#include "uvpp/core/callback.hpp"
#include "uvpp/uv.hpp"

namespace {

void leak_opendir_result_for_death_test() {
  uv::fs::raw::opendir_result result{0, reinterpret_cast<void *>(1)};
}

}

TEST(Uvpp2Core, mapsLibuvStatusesToErrorCodes) {
  auto success = uv::make_error_code(0);
  EXPECT_FALSE(success);

  auto failure = uv::make_error_code(UV_EINVAL);
  EXPECT_TRUE(failure);
  EXPECT_EQ(failure.value(), UV_EINVAL);
  EXPECT_EQ(&failure.category(), &uv::category());
  EXPECT_FALSE(failure.message().empty());
}

TEST(Uvpp2Core, checkThrowsUvppErrorOnLibuvFailure) {
  EXPECT_EQ(uv::throw_if_error(0), 0);

  try {
    uv::throw_if_error(UV_EINVAL);
    FAIL() << "uv::check should throw on negative libuv status";
  } catch (const uv::error &err) {
    EXPECT_EQ(err.code(), uv::make_error_code(UV_EINVAL));
  }
}

TEST(Uvpp2Core, resultUsesOneErrorGrammar) {
  uv::result ok;
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ok.ok());
  EXPECT_EQ(ok.status(), 0);
  EXPECT_FALSE(ok.error_code());

  uv::result failed{UV_ECONNREFUSED};
  EXPECT_FALSE(failed);
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(failed.status(), UV_ECONNREFUSED);
  EXPECT_EQ(failed.error_code(), uv::make_error_code(UV_ECONNREFUSED));
}

TEST(Uvpp2Core, loopCloseIsExplicitAndReportsBusyLoops) {
  uv::loop loop;
  uv::timer timer(loop);

  auto busy = loop.try_close();
  ASSERT_TRUE(busy);
  EXPECT_EQ(busy, uv::make_error_code(UV_EBUSY));
  EXPECT_THROW(loop.close(), uv::error);

  timer.close();
  loop.run();
  EXPECT_FALSE(loop.try_close());
}

TEST(Uvpp2Core, requestCallbacksAreOneShot) {
  {
    uv::write_request request;
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    bool called = false;

    request.set_callback([token, &called](uv::write_request&, uv::result) {
      called = true;
    });
    token.reset();
    EXPECT_FALSE(weak.expired());

    request.invoke(0);
    EXPECT_TRUE(called);
    EXPECT_TRUE(weak.expired());
  }

  {
    uv::connect_request request;
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    bool called = false;

    request.set_callback([token, &called](uv::connect_request&, uv::result) {
      called = true;
    });
    token.reset();

    request.invoke(0);
    EXPECT_TRUE(called);
    EXPECT_TRUE(weak.expired());
  }

  {
    uv::shutdown_request request;
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    bool called = false;

    request.set_callback([token, &called](uv::shutdown_request&, uv::result) {
      called = true;
    });
    token.reset();

    request.invoke(0);
    EXPECT_TRUE(called);
    EXPECT_TRUE(weak.expired());
  }

  {
    uv::udp_send_request request;
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    bool called = false;

    request.set_callback([token, &called](uv::udp_send_request&, uv::result) {
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
  EXPECT_DEATH(uv::detail::invoke_callback([] {
    throw 1;
  }), ".*");
}

TEST(Uvpp2CoreDeathTest, successfulOpendirResultMustBeConsumed) {
  EXPECT_DEATH(leak_opendir_result_for_death_test(), ".*");
}
#endif
