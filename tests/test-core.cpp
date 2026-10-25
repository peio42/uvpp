#include <concepts>
#include <cstdint>
#include <limits>
#include <system_error>
#include <memory>
#include <utility>

#include <uv.h>

#include "gtest/gtest.h"
#include "uvpp/core/callback.hpp"
#include "uvpp/uv.hpp"

namespace {

template<class T>
concept checked_status = requires(T status) {
  { uv::throw_if_error(status) } -> std::same_as<T>;
};

template<class T>
concept accepted_status = requires(T status) { uv::throw_if_error(status); };

enum foreign_status { foreign_error = -1 };

static_assert(checked_status<signed char>);
static_assert(checked_status<short>);
static_assert(checked_status<int>);
static_assert(checked_status<long>);
static_assert(checked_status<long long>);
static_assert(checked_status<ssize_t>);
static_assert(!accepted_status<unsigned int>);
static_assert(!accepted_status<bool>);
static_assert(!accepted_status<double>);
static_assert(!accepted_status<foreign_status>);
static_assert(accepted_status<uv_errno_t>);
static_assert(std::same_as<decltype(uv::throw_if_error(UV_EBADF)), int>);
static_assert(std::same_as<decltype(uv::throw_if_error(uv_errno_t{})), int>);

struct immovable_payload {
  explicit immovable_payload(int value) : number{value} {}
  immovable_payload(const immovable_payload&) = delete;
  immovable_payload(immovable_payload&&) = delete;
  int number;
};

template<class Result>
void expect_unchanged_result_error(const Result &outcome, uv::error_code code) {
  try {
    outcome.throw_if_error();
    FAIL() << "Error result must throw";
  } catch (const uv::error &error) {
    EXPECT_EQ(error.code(), static_cast<std::error_code>(code));
    EXPECT_EQ(&error.code().category(), &uv::category());
  }
  EXPECT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error(), code);
  EXPECT_THROW((void)outcome.value(), uv::error);
}

void leak_opendir_result_for_death_test() {
  uv::raw::fs::opendir_result result{0, reinterpret_cast<void *>(1)};
}

}

TEST(Uvpp2Core, mapsLibuvStatusesToErrorCodes) {
  auto success = uv::make_error_code(0);
  EXPECT_FALSE(success);

  auto failure = uv::make_error_code(UV_EINVAL);
  EXPECT_TRUE(failure);
  EXPECT_EQ(failure.native(), UV_EINVAL);
  const auto standard = static_cast<std::error_code>(failure);
  EXPECT_EQ(standard.value(), UV_EINVAL);
  EXPECT_EQ(&standard.category(), &uv::category());
  EXPECT_FALSE(standard.message().empty());
}

TEST(Uvpp2Core, checkThrowsUvppErrorOnLibuvFailure) {
  EXPECT_EQ(uv::throw_if_error(0), 0);
  EXPECT_EQ(uv::throw_if_error(uv_errno_t{}), 0);
  const int integer_error = UV_EINVAL;
  EXPECT_THROW(uv::throw_if_error(integer_error), uv::error);

  try {
    uv::throw_if_error(UV_EINVAL);
    FAIL() << "uv::throw_if_error should throw on negative libuv status";
  } catch (const uv::error &err) {
    EXPECT_EQ(err.code(), static_cast<std::error_code>(uv::make_error_code(UV_EINVAL)));
  }
}

TEST(Uvpp2Core, checkPreservesWideSuccessfulCountsAndThrowsWideErrors) {
  // On Windows ssize_t is 64-bit while int and uv_buf_t::len are 32-bit.
  // Success must be returned without first narrowing it into an error value.
  const auto count = static_cast<std::int64_t>(std::numeric_limits<int>::max()) + 1;
  EXPECT_EQ(uv::throw_if_error(count), count);
  EXPECT_EQ(uv::throw_if_error(std::numeric_limits<std::int64_t>::max()),
            std::numeric_limits<std::int64_t>::max());
  EXPECT_EQ(uv::throw_if_error(std::int64_t{0}), 0);

  try {
    (void)uv::throw_if_error(static_cast<std::int64_t>(UV_EINVAL));
    FAIL() << "wide negative libuv status must throw";
  } catch (const uv::error &error) {
    EXPECT_EQ(error.code().value(), static_cast<int>(UV_EINVAL));
    EXPECT_EQ(&error.code().category(), &uv::category());
  }
}

TEST(Uvpp2Core, voidResultUsesOneErrorGrammar) {
  static_assert(std::same_as<uv::status, uv::result<void>>);

  uv::status status;
  EXPECT_TRUE(status);

  uv::result<void> ok;
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ok.has_value());
  EXPECT_FALSE(ok.error());
  EXPECT_NO_THROW(ok.value());

  auto failed = uv::status::from_native(UV_ECONNREFUSED);
  EXPECT_FALSE(failed);
  EXPECT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), uv::make_error_code(UV_ECONNREFUSED));
  EXPECT_THROW(failed.value(), uv::error);

  uv::result<void> canceled{uv::make_error_code(UV_ECANCELED)};
  EXPECT_FALSE(canceled);
  EXPECT_EQ(canceled.error(), uv::make_error_code(UV_ECANCELED));
}

TEST(Uvpp2Core, valueResultSupportsMoveOnlyValues) {
  uv::result<std::unique_ptr<int>> ok{std::make_unique<int>(42)};
  static_assert(!std::copy_constructible<decltype(ok)>);
  EXPECT_TRUE(ok);
  EXPECT_EQ(*ok.value(), 42);

  auto value = std::move(ok).value();
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(*value, 42);

  uv::result<std::unique_ptr<int>> failed{uv::make_error_code(UV_ENOMEM)};
  EXPECT_FALSE(failed);
  EXPECT_EQ(failed.error().native(), UV_ENOMEM);
  EXPECT_THROW((void)failed.value(), uv::error);

  uv::result<uv::error_code> code_value{std::in_place, uv::make_error_code(UV_EINVAL)};
  EXPECT_TRUE(code_value);
  EXPECT_EQ(code_value.value(), uv::make_error_code(UV_EINVAL));
}

TEST(Uvpp2Core, throwIfErrorObservesSuccessWithoutConsumingPayload) {
  uv::result<std::unique_ptr<int>> outcome{std::make_unique<int>(42)};
  using result = decltype(outcome);
  static_assert(std::same_as<decltype(outcome.throw_if_error()), void>);
  static_assert(std::same_as<decltype(std::as_const(outcome).throw_if_error()), void>);
  static_assert(std::same_as<decltype(std::declval<result&>().value()), std::unique_ptr<int>&>);
  static_assert(std::same_as<decltype(std::declval<const result&>().value()), const std::unique_ptr<int>&>);
  static_assert(std::same_as<decltype(std::declval<result&&>().value()), std::unique_ptr<int>&&>);
  static_assert(std::same_as<decltype(std::declval<const result&&>().value()), const std::unique_ptr<int>&&>);
  auto *payload = outcome.value().get();
  EXPECT_NO_THROW(outcome.throw_if_error());
  EXPECT_NO_THROW(std::as_const(outcome).throw_if_error());
  EXPECT_NO_THROW(std::move(outcome).throw_if_error());
  EXPECT_NO_THROW(std::move(std::as_const(outcome)).throw_if_error());
  ASSERT_TRUE(outcome);
  EXPECT_EQ(outcome.value().get(), payload);
  EXPECT_EQ(*outcome.value(), 42);
  auto consumed = std::move(outcome).value();
  EXPECT_EQ(consumed.get(), payload);

  const uv::result<immovable_payload> immovable{std::in_place, 7};
  EXPECT_NO_THROW(immovable.throw_if_error());
  EXPECT_EQ(immovable.value().number, 7);
  const uv::result<uv::error_code> error_as_payload{std::in_place, uv::make_error_code(UV_EINVAL)};
  EXPECT_NO_THROW(error_as_payload.throw_if_error());
  EXPECT_EQ(error_as_payload.value(), uv::make_error_code(UV_EINVAL));
}

TEST(Uvpp2Core, throwIfErrorOnStatusReturnsVoidWithoutChangingSuccess) {
  const uv::status outcome;
  static_assert(std::same_as<decltype(outcome.throw_if_error()), void>);
  static_assert(std::same_as<decltype(outcome.value()), void>);
  EXPECT_NO_THROW(outcome.throw_if_error());
  EXPECT_NO_THROW(std::move(outcome).throw_if_error());
  EXPECT_NO_THROW(outcome.value());
  EXPECT_TRUE(outcome.has_value());
  EXPECT_FALSE(outcome.error());
}

TEST(Uvpp2Core, throwIfErrorPreservesFailureAndMatchesValueException) {
  for (const int native : {UV_EINVAL, UV_ECANCELED, UV_EIO}) {
    const auto code = uv::make_error_code(native);
    const uv::result<std::unique_ptr<int>> value_result{code};
    const uv::status void_result{code};
    expect_unchanged_result_error(value_result, code);
    expect_unchanged_result_error(void_result, code);
    EXPECT_THROW(std::move(value_result).throw_if_error(), uv::error);
    EXPECT_THROW(std::move(void_result).throw_if_error(), uv::error);
    EXPECT_EQ(value_result.error(), code);
    EXPECT_EQ(void_result.error(), code);
  }
}

TEST(Uvpp2Core, loopCloseIsExplicitAndReportsBusyLoops) {
  uv::loop loop;
  uv::raw::timer timer(loop);

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

    request.set_callback([token, &called](uv::write_request&, uv::status) {
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

    request.set_callback([token, &called](uv::connect_request&, uv::status) {
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

    request.set_callback([token, &called](uv::shutdown_request&, uv::status) {
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

    request.set_callback([token, &called](uv::udp_send_request&, uv::status) {
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

// ---------------------------------------------------------------------------
// Address to_string
// ---------------------------------------------------------------------------

TEST(Uvpp2Address, ipv4ToStringReturnsIPPart) {
  uv::ipv4 addr{"127.0.0.1", 8080};
  EXPECT_EQ(addr.to_string(), "127.0.0.1");
}

TEST(Uvpp2Address, ipv4ToStringIgnoresPort) {
  uv::ipv4 a{"10.0.0.1", 1234};
  uv::ipv4 b{"10.0.0.1", 5678};
  EXPECT_EQ(a.to_string(), b.to_string());
}

TEST(Uvpp2Address, ipv6ToStringReturnsIPPart) {
  uv::ipv6 addr{"::1", 8080};
  EXPECT_EQ(addr.to_string(), "::1");
}

TEST(Uvpp2Address, ipv6ToStringIgnoresPort) {
  uv::ipv6 a{"::1", 1234};
  uv::ipv6 b{"::1", 5678};
  EXPECT_EQ(a.to_string(), b.to_string());
}
