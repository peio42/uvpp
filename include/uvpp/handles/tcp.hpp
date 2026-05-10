#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/stream.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/requests/connect.hpp"

namespace uvpp {

  class tcp final : public stream<tcp, uv_tcp_t> {
  public:
    explicit tcp(loop &l) {
      throw_if_error(uv_tcp_init(l.native(), native()));
    }

    explicit tcp(loop_view l) {
      throw_if_error(uv_tcp_init(l.native(), native()));
    }

    void bind(const ipv4 &addr, unsigned int flags = 0) {
      throw_if_error(uv_tcp_bind(native(), addr.native_sockaddr(), flags));
    }

    void bind(const ipv6 &addr, unsigned int flags = 0) {
      throw_if_error(uv_tcp_bind(native(), addr.native_sockaddr(), flags));
    }

    void sockname(sockaddr_in &addr) {
      int len = sizeof(addr);
      throw_if_error(uv_tcp_getsockname(native(), reinterpret_cast<sockaddr *>(&addr), &len));
    }

    void connect(connect_request &request, const ipv4 &addr, connect_request::callback callback) {
      request.set_callback(std::move(callback));
      throw_if_error(uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), &tcp::connect_trampoline));
    }

    void connect(connect_request &request, const ipv6 &addr, connect_request::callback callback) {
      request.set_callback(std::move(callback));
      throw_if_error(uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), &tcp::connect_trampoline));
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

  private:
    static void connect_trampoline(uv_connect_t *raw, int status) noexcept {
      connect_request::from_native(raw).invoke(status);
    }
  };

}
