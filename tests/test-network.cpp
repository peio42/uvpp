#include <netdb.h>
#include <string>
#include <type_traits>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

void on_static_getaddrinfo(uv::getaddrinfo_request &, uv::getaddrinfo_result) {}
void on_static_getnameinfo(uv::getnameinfo_request &, uv::getnameinfo_result) {}

}

TEST(Uvpp2Network, resolvesAddressInfo) {
  uv::loop loop;
  uv::getaddrinfo_request request;

  int marker = 42;
  request.user_data(marker);

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  bool called = false;

  uv::getaddrinfo(loop, request, "localhost", "80", &hints,
    [&](uv::getaddrinfo_request &completed, uv::getaddrinfo_result result) {
      called = true;

      EXPECT_EQ(&completed, &request);
      EXPECT_EQ(completed.user_data<int>(), &marker);
      EXPECT_EQ(completed.type(), uv::request_type::getaddrinfo);
      ASSERT_TRUE(result);

      std::size_t count = 0;
      for (uv::addrinfo_view entry : result) {
        ++count;
        EXPECT_EQ(entry.family(), AF_INET);
        EXPECT_EQ(entry.socket_type(), SOCK_STREAM);
        EXPECT_NE(entry.address(), nullptr);
        EXPECT_GT(entry.address_length(), 0u);
      }

      EXPECT_GT(count, 0u);
    });

  loop.run();

  EXPECT_TRUE(called);
  loop.close();
}

TEST(Uvpp2Network, resolvesNameInfo) {
  uv::loop loop;
  uv::getnameinfo_request request;

  bool called = false;
  uv::ipv4 address{"127.0.0.1", 443};

  uv::getnameinfo(loop, request, address, NI_NUMERICHOST | NI_NUMERICSERV,
    [&](uv::getnameinfo_request &completed, uv::getnameinfo_result result) {
      called = true;

      EXPECT_EQ(&completed, &request);
      EXPECT_EQ(completed.type(), uv::request_type::getnameinfo);
      ASSERT_TRUE(result);
      EXPECT_EQ(result.hostname(), "127.0.0.1");
      EXPECT_EQ(result.service(), "443");
    });

  loop.run();

  EXPECT_TRUE(called);
  loop.close();
}

TEST(Uvpp2Network, exposesStaticDnsOverloads) {
  using getaddrinfo_static_fn = void (*)(uv::loop&, uv::getaddrinfo_request&,
                                        std::string_view, std::string_view,
                                        const addrinfo*);
  using getnameinfo_static_fn = void (*)(uv::loop&, uv::getnameinfo_request&,
                                        const uv::ipv4&, int);

  getaddrinfo_static_fn resolve = &uv::getaddrinfo_static<on_static_getaddrinfo>;
  getnameinfo_static_fn reverse = &uv::getnameinfo_static<on_static_getnameinfo>;

  (void)resolve;
  (void)reverse;
}

TEST(Uvpp2Network, immediateGetnameinfoFailureLeavesRequestReusable) {
  uv::loop loop;
  uv::getnameinfo_request request;

  bool failed_callback_called = false;
  EXPECT_THROW(
    uv::getnameinfo(loop, request, static_cast<const sockaddr *>(nullptr), 0,
      [&](uv::getnameinfo_request &, uv::getnameinfo_result) {
        failed_callback_called = true;
      }),
    uv::error);
  EXPECT_FALSE(failed_callback_called);

  bool reused_callback_called = false;
  uv::ipv4 address{"127.0.0.1", 443};

  uv::getnameinfo(loop, request, address, NI_NUMERICHOST | NI_NUMERICSERV,
    [&](uv::getnameinfo_request &completed, uv::getnameinfo_result result) {
      reused_callback_called = true;

      EXPECT_EQ(&completed, &request);
      ASSERT_TRUE(result);
      EXPECT_EQ(result.hostname(), "127.0.0.1");
      EXPECT_EQ(result.service(), "443");
    });

  loop.run();

  EXPECT_TRUE(reused_callback_called);
  loop.close();
}

#if UVPP_HAS_IF_INDEX_TO_NAME
TEST(Uvpp2Network, invalidInterfaceIndexThrows) {
  EXPECT_THROW((void)uv::interface_name(0), uv::error);
#if UVPP_HAS_IF_INDEX_TO_IID
  EXPECT_THROW((void)uv::interface_identifier(0), uv::error);
#endif
}
#endif
