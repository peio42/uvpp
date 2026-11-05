#include <uv.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <memory>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {
void on_static_getaddrinfo(uv::raw::getaddrinfo_request &request, uv::raw::getaddrinfo_result result) {
  auto &called = *request.user_data<bool>();
  EXPECT_FALSE(request.pending());
  EXPECT_TRUE(result);
  called = true;
}
void on_static_getnameinfo(uv::raw::getnameinfo_request &request, uv::raw::getnameinfo_result result) {
  auto &called = *request.user_data<bool>();
  EXPECT_FALSE(request.pending());
  EXPECT_TRUE(result);
  EXPECT_EQ(result.hostname(), "127.0.0.1");
  called = true;
}

struct dns_transition_state {
  uv::loop *loop;
  std::weak_ptr<int> first_capture;
  int first_calls = 0;
  int static_calls = 0;
  int final_calls = 0;
  int rejected_calls = 0;
};

void transition_addrinfo_static(uv::raw::getaddrinfo_request &request, uv::raw::getaddrinfo_result result) {
  auto &state = *request.user_data<dns_transition_state>();
  EXPECT_TRUE(result);
  EXPECT_FALSE(request.pending());
  EXPECT_TRUE(state.first_capture.expired());
  ++state.static_calls;

  auto capture = std::make_shared<int>(1);
  std::weak_ptr<int> rejected_capture = capture;
  const auto rejected = uv::raw::getaddrinfo(*state.loop, request, nullptr, nullptr,
      [&, capture](auto&, auto) { ++state.rejected_calls; });
  capture.reset();
  EXPECT_EQ(rejected.error(), uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(rejected_capture.expired());
  EXPECT_FALSE(request.pending());

  EXPECT_TRUE(uv::raw::getaddrinfo(*state.loop, request, "127.0.0.1", "443",
      [&](auto &completed, uv::raw::getaddrinfo_result next) {
        EXPECT_FALSE(completed.pending());
        EXPECT_TRUE(next);
        ++state.final_calls;
      }));
}

void transition_nameinfo_static(uv::raw::getnameinfo_request &request, uv::raw::getnameinfo_result result) {
  auto &state = *request.user_data<dns_transition_state>();
  EXPECT_TRUE(result);
  EXPECT_EQ(result.service(), "80");
  EXPECT_FALSE(request.pending());
  EXPECT_TRUE(state.first_capture.expired());
  ++state.static_calls;

  auto capture = std::make_shared<int>(1);
  std::weak_ptr<int> rejected_capture = capture;
  const auto rejected = uv::raw::getnameinfo(*state.loop, request, nullptr, 0,
      [&, capture](auto&, auto) { ++state.rejected_calls; });
  capture.reset();
  EXPECT_EQ(rejected.error(), uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(rejected_capture.expired());
  EXPECT_FALSE(request.pending());

  uv::ipv4 address{"127.0.0.1", 443};
  EXPECT_TRUE(uv::raw::getnameinfo(*state.loop, request, address, NI_NUMERICHOST | NI_NUMERICSERV,
      [&](auto &completed, uv::raw::getnameinfo_result next) {
        EXPECT_FALSE(completed.pending());
        EXPECT_TRUE(next);
        EXPECT_EQ(next.service(), "443");
        ++state.final_calls;
      }));
}
}

TEST(RawDns, addressInfoOwnsNativeResultAndSupportsReentrantReuse) {
  uv::loop loop;
  uv::raw::getaddrinfo_request request;
  int marker = 42;
  request.user_data(marker);
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  uv::raw::getaddrinfo_result retained;
  bool called = false, called_again = false;
  ASSERT_TRUE(uv::raw::getaddrinfo(loop, request, "127.0.0.1", "80", &hints,
    [&](uv::raw::getaddrinfo_request &completed, uv::raw::getaddrinfo_result result) {
      EXPECT_FALSE(completed.pending());
      EXPECT_EQ(completed.user_data<int>(), &marker);
      EXPECT_EQ(completed.type(), uv::request_type::getaddrinfo);
      ASSERT_TRUE(result);
      retained = std::move(result);
      called = true;
      EXPECT_TRUE(uv::raw::getaddrinfo(loop, completed, "127.0.0.1", "443", &hints,
        [&](uv::raw::getaddrinfo_request &, uv::raw::getaddrinfo_result second) {
          EXPECT_TRUE(second);
          called_again = true;
        }));
    }));
  EXPECT_TRUE(request.pending());
  EXPECT_EQ(uv::raw::getaddrinfo(loop, request, "127.0.0.1", "80", nullptr,
      [](auto&, auto) {}).error(), uv::make_error_code(UV_EBUSY));
  loop.run();
  EXPECT_TRUE(called);
  EXPECT_TRUE(called_again);
  EXPECT_FALSE(request.pending());
  auto copies = retained.to_vector();
  ASSERT_FALSE(copies.empty());
  EXPECT_EQ(copies.front().address.port(), 80);
  retained.reset();
  EXPECT_EQ(copies.front().address.port(), 80);
  loop.close();
}

TEST(RawDns, nameInfoBorrowsNativeStringsAndAllowsReuse) {
  uv::loop loop;
  uv::raw::getnameinfo_request request;
  uv::ipv4 address{"127.0.0.1", 443};
  std::string owned;
  bool called = false;
  ASSERT_TRUE(uv::raw::getnameinfo(loop, request, address.native_sockaddr(), NI_NUMERICHOST | NI_NUMERICSERV,
    [&](uv::raw::getnameinfo_request &completed, uv::raw::getnameinfo_result result) {
      EXPECT_FALSE(completed.pending());
      EXPECT_EQ(completed.type(), uv::request_type::getnameinfo);
      ASSERT_TRUE(result);
      EXPECT_EQ(result.hostname(), "127.0.0.1");
      EXPECT_EQ(result.service(), "443");
      EXPECT_EQ(result.native_hostname(), completed.native()->host);
      owned = result.hostname();
      called = true;
    }));
  loop.run();
  EXPECT_TRUE(called);
  EXPECT_EQ(owned, "127.0.0.1");
  loop.close();
}

TEST(RawDns, nativeRejectionReleasesCallbackAndRequestCanBeReused) {
  uv::loop loop;
  uv::raw::getnameinfo_request request;
  auto owner = std::make_shared<int>(1);
  std::weak_ptr<int> weak = owner;
  auto result = uv::raw::getnameinfo(loop, request, nullptr, 0, [owner](auto&, auto) {});
  owner.reset();
  EXPECT_EQ(result.error(), uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(weak.expired());
  EXPECT_FALSE(request.pending());
  uv::ipv4 address{"127.0.0.1", 443};
  bool called = false;
  request.user_data(called);
  ASSERT_TRUE(uv::raw::getnameinfo_static<on_static_getnameinfo>(loop, request,
      address.native_sockaddr(), NI_NUMERICHOST | NI_NUMERICSERV));
  loop.run();
  EXPECT_TRUE(called);
  loop.close();
}

TEST(RawDns, staticAddressLookupAndImmediateFailure) {
  uv::loop loop;
  uv::raw::getaddrinfo_request request;
  bool called = false;
  request.user_data(called);
  EXPECT_EQ(uv::raw::getaddrinfo_static<on_static_getaddrinfo>(loop, request, nullptr, nullptr).error(),
            uv::make_error_code(UV_EINVAL));
  EXPECT_FALSE(request.pending());
  ASSERT_TRUE(uv::raw::getaddrinfo_static<on_static_getaddrinfo>(loop, request, "127.0.0.1", "80"));
  loop.run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(request.pending());
  loop.close();
}

TEST(RawDns, completionFailureAndCancellationKeepRequestUntilCallback) {
  uv::loop loop;
  uv::raw::getaddrinfo_request request;
  addrinfo hints{};
  hints.ai_flags = AI_NUMERICHOST;
  bool called = false;
  ASSERT_TRUE(uv::raw::getaddrinfo(loop, request, "not-an-ip-address", "80", &hints,
      [&](uv::raw::getaddrinfo_request &completed, uv::raw::getaddrinfo_result result) {
        EXPECT_FALSE(completed.pending());
        EXPECT_FALSE(result);
        EXPECT_NE(result.raw_status(), 0);
        EXPECT_EQ(result.native(), nullptr);
        called = true;
      }));
  const auto cancellation = request.cancel();
  if (!cancellation) EXPECT_EQ(cancellation.error(), uv::make_error_code(UV_EBUSY));
  EXPECT_TRUE(request.pending());
  loop.run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(request.pending());
  loop.close();
}

TEST(RawDns, reverseLookupCanResubmitFromItsCallback) {
  uv::loop loop;
  uv::raw::getnameinfo_request request;
  uv::ipv4 first{"127.0.0.1", 443};
  uv::ipv4 second{"127.0.0.1", 80};
  std::string first_hostname, first_service, second_service;
  ASSERT_TRUE(uv::raw::getnameinfo(loop, request, first, NI_NUMERICHOST | NI_NUMERICSERV,
      [&](uv::raw::getnameinfo_request &completed, uv::raw::getnameinfo_result result) {
        ASSERT_TRUE(result);
        {
          const std::string_view hostname = result.hostname();
          const std::string_view service = result.service();
          EXPECT_EQ(hostname.data(), completed.native()->host);
          EXPECT_EQ(service.data(), completed.native()->service);
          first_hostname.assign(hostname);
          first_service.assign(service);
        } // Borrowed views must not be used after the request is reused.
        EXPECT_TRUE(uv::raw::getnameinfo(loop, completed, second, NI_NUMERICHOST | NI_NUMERICSERV,
            [&](auto &, uv::raw::getnameinfo_result next) {
              EXPECT_TRUE(next);
              second_service = next.service();
            }));
      }));
  loop.run();
  EXPECT_EQ(first_hostname, "127.0.0.1");
  EXPECT_EQ(first_service, "443");
  EXPECT_EQ(second_service, "80");
  loop.close();
}

TEST(RawDns, addressInfoChangesDispatchAcrossReentrantSubmissionsAndRejection) {
  uv::loop loop;
  uv::raw::getaddrinfo_request request;
  dns_transition_state state{&loop};
  request.user_data(state);
  auto capture = std::make_shared<int>(1);
  state.first_capture = capture;
  ASSERT_TRUE(uv::raw::getaddrinfo(loop, request, "127.0.0.1", "80",
      [&, capture](auto &completed, uv::raw::getaddrinfo_result result) {
        EXPECT_TRUE(result);
        EXPECT_FALSE(completed.pending());
        ++state.first_calls;
        EXPECT_TRUE(uv::raw::getaddrinfo_static<transition_addrinfo_static>(
            loop, completed, "127.0.0.1", "80"));
      }));
  capture.reset();
  loop.run();
  EXPECT_EQ(state.first_calls, 1);
  EXPECT_EQ(state.static_calls, 1);
  EXPECT_EQ(state.final_calls, 1);
  EXPECT_EQ(state.rejected_calls, 0);
  EXPECT_FALSE(request.pending());
  loop.close();
}

TEST(RawDns, nameInfoChangesDispatchAcrossReentrantSubmissionsAndRejection) {
  uv::loop loop;
  uv::raw::getnameinfo_request request;
  dns_transition_state state{&loop};
  request.user_data(state);
  auto capture = std::make_shared<int>(1);
  state.first_capture = capture;
  uv::ipv4 address{"127.0.0.1", 80};
  ASSERT_TRUE(uv::raw::getnameinfo(loop, request, address, NI_NUMERICHOST | NI_NUMERICSERV,
      [&, capture](auto &completed, uv::raw::getnameinfo_result result) {
        EXPECT_TRUE(result);
        EXPECT_FALSE(completed.pending());
        ++state.first_calls;
        EXPECT_TRUE(uv::raw::getnameinfo_static<transition_nameinfo_static>(
            loop, completed, address, NI_NUMERICHOST | NI_NUMERICSERV));
      }));
  capture.reset();
  loop.run();
  EXPECT_EQ(state.first_calls, 1);
  EXPECT_EQ(state.static_calls, 1);
  EXPECT_EQ(state.final_calls, 1);
  EXPECT_EQ(state.rejected_calls, 0);
  EXPECT_FALSE(request.pending());
  loop.close();
}

#if UVPP_HAS_IF_INDEX_TO_NAME
TEST(Uvpp2Network, invalidInterfaceIndexThrows) {
  EXPECT_THROW((void)uv::interface_name(0), uv::error);
}
#endif

#if UVPP_HAS_IF_INDEX_TO_IID
TEST(Uvpp2Network, interfaceIdentifierPreservesPlatformSemantics) {
#ifdef _WIN32
  // Windows formats the numeric index for an IPv6 scope identifier; this is
  // not an interface lookup and does not validate whether the index exists.
  EXPECT_EQ(uv::interface_identifier(0), "0");
  EXPECT_EQ(uv::interface_identifier(42), "42");
#else
  // Unix delegates to if_indextoname(), which rejects the zero index.
  EXPECT_THROW((void)uv::interface_identifier(0), uv::error);
#endif
}
#endif
