#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/stream.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/socket_address.hpp"
#include "uvpp/requests/connect.hpp"

namespace uv {

  enum class tcp_socket_family : unsigned int {
    ipv4 = AF_INET,
    ipv6 = AF_INET6
  };

  enum class tcp_bind_flag : unsigned int {
    ipv6_only = UV_TCP_IPV6ONLY
#if UV_VERSION_HEX >= 0x013100
    ,
    reuse_port = UV_TCP_REUSEPORT
#endif
  };

  constexpr unsigned int operator|(tcp_bind_flag lhs, tcp_bind_flag rhs) noexcept {
    return static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs);
  }

  constexpr unsigned int operator|(unsigned int lhs, tcp_bind_flag rhs) noexcept {
    return lhs | static_cast<unsigned int>(rhs);
  }

  class tcp final : public stream<tcp, uv_tcp_t> {
  public:
    explicit tcp(loop &l) {
      throw_if_error(uv_tcp_init(l.native(), native()));
    }

    explicit tcp(loop_view l) {
      throw_if_error(uv_tcp_init(l.native(), native()));
    }

    tcp(loop &l, tcp_socket_family family) {
      throw_if_error(uv_tcp_init_ex(l.native(), native(), static_cast<unsigned int>(family)));
    }

    tcp(loop_view l, tcp_socket_family family) {
      throw_if_error(uv_tcp_init_ex(l.native(), native(), static_cast<unsigned int>(family)));
    }

    void open(uv_os_sock_t socket) {
      throw_if_error(uv_tcp_open(native(), socket));
    }

    void bind(const ipv4 &addr, unsigned int flags = 0) {
      throw_if_error(uv_tcp_bind(native(), addr.native_sockaddr(), flags));
    }

    void bind(const ipv6 &addr, unsigned int flags = 0) {
      throw_if_error(uv_tcp_bind(native(), addr.native_sockaddr(), flags));
    }

    void bind(const ipv4 &addr, tcp_bind_flag flag) {
      bind(addr, static_cast<unsigned int>(flag));
    }

    void bind(const ipv6 &addr, tcp_bind_flag flag) {
      bind(addr, static_cast<unsigned int>(flag));
    }

    socket_address sockname() {
      socket_address addr;
      throw_if_error(uv_tcp_getsockname(native(), addr.native(), addr.native_len()));
      return addr;
    }

    socket_address peername() {
      socket_address addr;
      throw_if_error(uv_tcp_getpeername(native(), addr.native(), addr.native_len()));
      return addr;
    }

    void connect(connect_request &request, const ipv4 &addr, connect_request::callback callback) {
      detail::submit_request(request, std::move(callback), [&] {
        return uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), &tcp::connect_trampoline);
      });
    }

    void connect(connect_request &request, const ipv6 &addr, connect_request::callback callback) {
      detail::submit_request(request, std::move(callback), [&] {
        return uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), &tcp::connect_trampoline);
      });
    }

    template<auto Callback>
    void connect_static(connect_request &request, const ipv4 &addr) {
      throw_if_error(uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), [](uv_connect_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(connect_request::from_native(raw), result{status});
      }));
    }

    template<auto Callback>
    void connect_static(connect_request &request, const ipv6 &addr) {
      throw_if_error(uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), [](uv_connect_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(connect_request::from_native(raw), result{status});
      }));
    }

    void close_reset() {
      throw_if_error(uv_tcp_close_reset(native(), nullptr));
    }

    void close_reset(close_callback callback) {
      set_close_callback(std::move(callback));
      try {
        throw_if_error(uv_tcp_close_reset(native(), &tcp::close_reset_trampoline));
      } catch (...) {
        set_close_callback({});
        throw;
      }
    }

    template<auto Callback>
    void close_reset_static() {
      throw_if_error(uv_tcp_close_reset(native(), [](uv_handle_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(tcp::from_native(raw));
      }));
    }

    void no_delay(bool enable) {
      throw_if_error(uv_tcp_nodelay(native(), enable ? 1 : 0));
    }

    void keep_alive(bool enable, unsigned int delay = 0) {
      throw_if_error(uv_tcp_keepalive(native(), enable ? 1 : 0, delay));
    }

    void simultaneous_accepts(bool enable) {
      throw_if_error(uv_tcp_simultaneous_accepts(native(), enable ? 1 : 0));
    }

  private:
    static void close_reset_trampoline(uv_handle_t *raw) noexcept {
      basic_handle<tcp, uv_tcp_t>::close_trampoline(raw);
    }

    static void connect_trampoline(uv_connect_t *raw, int status) noexcept {
      connect_request::from_native(raw).invoke(status);
    }
  };

}
