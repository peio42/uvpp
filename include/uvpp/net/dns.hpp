#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/socket_address.hpp"
#include "uvpp/requests/dns.hpp"

namespace uv {

  namespace detail {

    inline void getaddrinfo_trampoline(uv_getaddrinfo_t *raw, int status, addrinfo *addresses) noexcept {
      getaddrinfo_request::from_native(raw).invoke(status, addresses);
    }

    inline void getnameinfo_trampoline(uv_getnameinfo_t *raw, int status, const char *hostname,
                                       const char *service) noexcept {
      getnameinfo_request::from_native(raw).invoke(status, hostname, service);
    }

    inline void submit_getaddrinfo(loop_view loop, getaddrinfo_request &request,
                                   std::optional<std::string_view> node,
                                   std::optional<std::string_view> service,
                                   const addrinfo *hints,
                                   getaddrinfo_request::callback callback) {
      request.set_inputs(node, service, hints);
      detail::submit_request(request, std::move(callback),
        [&] {
          return uv_getaddrinfo(loop.native(), request.native(), getaddrinfo_trampoline,
                                request.node_arg(), request.service_arg(), request.hints_arg());
        },
        [&] { request.clear_inputs(); });
    }

    template<auto Callback>
    void submit_getaddrinfo_static(loop_view loop, getaddrinfo_request &request,
                                   std::optional<std::string_view> node,
                                   std::optional<std::string_view> service,
                                   const addrinfo *hints) {
      request.set_inputs(node, service, hints);
      detail::submit_with_rollback(
        [&] {
          return uv_getaddrinfo(loop.native(), request.native(),
            [](uv_getaddrinfo_t *raw, int status, addrinfo *addresses) noexcept {
              getaddrinfo_request::from_native(raw).template invoke_static<Callback>(status, addresses);
            },
            request.node_arg(), request.service_arg(), request.hints_arg());
        },
        [&] { request.clear_inputs(); });
    }

    inline void submit_getnameinfo(loop_view loop, getnameinfo_request &request, const sockaddr *addr,
                                   int flags, getnameinfo_request::callback callback) {
      request.set_address(addr);
      detail::submit_request(request, std::move(callback),
        [&] {
          return uv_getnameinfo(loop.native(), request.native(), getnameinfo_trampoline,
                                request.address_arg(), flags);
        },
        [&] { request.clear_address(); });
    }

    template<auto Callback>
    void submit_getnameinfo_static(loop_view loop, getnameinfo_request &request, const sockaddr *addr,
                                   int flags) {
      request.set_address(addr);
      detail::submit_with_rollback(
        [&] {
          return uv_getnameinfo(loop.native(), request.native(),
            [](uv_getnameinfo_t *raw, int status, const char *hostname, const char *service) noexcept {
              getnameinfo_request::from_native(raw).template invoke_static<Callback>(status, hostname, service);
            },
            request.address_arg(), flags);
        },
        [&] { request.clear_address(); });
    }

  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    detail::submit_getaddrinfo(loop, request, std::optional<std::string_view>{node},
                               std::optional<std::string_view>{service}, hints, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, node, service, hints, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop, request, node, service, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, node, service, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    detail::submit_getaddrinfo(loop, request, std::nullopt,
                               std::optional<std::string_view>{service}, hints, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, nullptr, service, hints, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop, request, nullptr, service, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, nullptr, service, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    detail::submit_getaddrinfo(loop, request, std::optional<std::string_view>{node},
                               std::nullopt, hints, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, node, nullptr, hints, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop, request, node, nullptr, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, node, nullptr, nullptr, std::move(callback));
  }

  template<auto Callback>
  void getaddrinfo_static(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          const addrinfo *hints = nullptr) {
    detail::submit_getaddrinfo_static<Callback>(loop, request, std::optional<std::string_view>{node},
                                                std::optional<std::string_view>{service}, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          const addrinfo *hints = nullptr) {
    getaddrinfo_static<Callback>(loop.view(), request, node, service, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop_view loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          const addrinfo *hints = nullptr) {
    detail::submit_getaddrinfo_static<Callback>(loop, request, std::nullopt,
                                                std::optional<std::string_view>{service}, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop &loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          const addrinfo *hints = nullptr) {
    getaddrinfo_static<Callback>(loop.view(), request, nullptr, service, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          const addrinfo *hints = nullptr) {
    detail::submit_getaddrinfo_static<Callback>(loop, request, std::optional<std::string_view>{node},
                                                std::nullopt, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          const addrinfo *hints = nullptr) {
    getaddrinfo_static<Callback>(loop.view(), request, node, nullptr, hints);
  }

  inline void getnameinfo(loop_view loop, getnameinfo_request &request, const sockaddr *addr,
                          int flags, getnameinfo_request::callback callback) {
    detail::submit_getnameinfo(loop, request, addr, flags, std::move(callback));
  }

  inline void getnameinfo(loop &loop, getnameinfo_request &request, const sockaddr *addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop.view(), request, addr, flags, std::move(callback));
  }

  inline void getnameinfo(loop_view loop, getnameinfo_request &request, const ipv4 &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop, request, addr.native_sockaddr(), flags, std::move(callback));
  }

  inline void getnameinfo(loop &loop, getnameinfo_request &request, const ipv4 &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop.view(), request, addr, flags, std::move(callback));
  }

  inline void getnameinfo(loop_view loop, getnameinfo_request &request, const ipv6 &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop, request, addr.native_sockaddr(), flags, std::move(callback));
  }

  inline void getnameinfo(loop &loop, getnameinfo_request &request, const ipv6 &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop.view(), request, addr, flags, std::move(callback));
  }

  inline void getnameinfo(loop_view loop, getnameinfo_request &request, const socket_address &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop, request, addr.native(), flags, std::move(callback));
  }

  inline void getnameinfo(loop &loop, getnameinfo_request &request, const socket_address &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop.view(), request, addr, flags, std::move(callback));
  }

  template<auto Callback>
  void getnameinfo_static(loop_view loop, getnameinfo_request &request, const sockaddr *addr,
                          int flags) {
    detail::submit_getnameinfo_static<Callback>(loop, request, addr, flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop &loop, getnameinfo_request &request, const sockaddr *addr,
                          int flags) {
    getnameinfo_static<Callback>(loop.view(), request, addr, flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop_view loop, getnameinfo_request &request, const ipv4 &addr,
                          int flags) {
    getnameinfo_static<Callback>(loop, request, addr.native_sockaddr(), flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop &loop, getnameinfo_request &request, const ipv4 &addr,
                          int flags) {
    getnameinfo_static<Callback>(loop.view(), request, addr, flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop_view loop, getnameinfo_request &request, const ipv6 &addr,
                          int flags) {
    getnameinfo_static<Callback>(loop, request, addr.native_sockaddr(), flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop &loop, getnameinfo_request &request, const ipv6 &addr,
                          int flags) {
    getnameinfo_static<Callback>(loop.view(), request, addr, flags);
  }

}
