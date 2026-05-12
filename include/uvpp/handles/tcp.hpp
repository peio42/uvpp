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

  class tcp final : public stream<tcp, uv_tcp_t> {
  public:
    explicit tcp(loop &l) {
      throw_if_error(uv_tcp_init(l.native(), native()));
    }

    explicit tcp(loop_view l) {
      throw_if_error(uv_tcp_init(l.native(), native()));
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
    static void connect_trampoline(uv_connect_t *raw, int status) noexcept {
      connect_request::from_native(raw).invoke(status);
    }
  };

}
