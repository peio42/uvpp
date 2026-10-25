#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/handles/stream.hpp"
#include "uvpp/detail/initializable_handle.hpp"
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
#if UVPP_HAS_TCP_REUSEPORT
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

}

namespace uv::raw {

  class tcp final : public ::uv::detail::initializable_handle<tcp, uv_tcp_t, stream<tcp, uv_tcp_t>> {
    using base = ::uv::detail::initializable_handle<tcp, uv_tcp_t, stream<tcp, uv_tcp_t>>;
  public:
    tcp() noexcept = default;

    explicit tcp(loop &l) : tcp(l.view()) {}
    explicit tcp(loop_view l) { init(l).throw_if_error(); }

    status init(loop &l) noexcept { return init(l.view()); }
    status init(loop_view l) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      return finish_init(uv_tcp_init(l.native(), native()));
    }

#if UVPP_HAS_TCP_INIT_EX
    tcp(loop &l, tcp_socket_family family) : tcp(l.view(), family) {}
    tcp(loop_view l, tcp_socket_family family) { init(l, family).throw_if_error(); }

    status init(loop &l, tcp_socket_family family) noexcept { return init(l.view(), family); }
    status init(loop_view l, tcp_socket_family family) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      return finish_init(uv_tcp_init_ex(l.native(), native(), static_cast<unsigned int>(family)));
    }
#endif

    status open(uv_os_sock_t socket) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return status::from_native(uv_tcp_open(native(), socket));
    }

    status bind(const ipv4 &addr, unsigned int flags = 0) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return status::from_native(uv_tcp_bind(native(), addr.native_sockaddr(), flags));
    }

    status bind(const ipv6 &addr, unsigned int flags = 0) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return status::from_native(uv_tcp_bind(native(), addr.native_sockaddr(), flags));
    }

    status bind(const ipv4 &addr, tcp_bind_flag flag) noexcept {
      return bind(addr, static_cast<unsigned int>(flag));
    }

    status bind(const ipv6 &addr, tcp_bind_flag flag) noexcept {
      return bind(addr, static_cast<unsigned int>(flag));
    }

    result<socket_address> sockname() const noexcept {
      if (!initialized() || closing()) return result<socket_address>{make_error_code(UV_EBADF)};
      socket_address addr;
      const auto code = uv_tcp_getsockname(native(), addr.native(), addr.native_len());
      if (code < 0) return result<socket_address>{make_error_code(code)};
      return result<socket_address>{addr};
    }

    result<socket_address> peername() const noexcept {
      if (!initialized() || closing()) return result<socket_address>{make_error_code(UV_EBADF)};
      socket_address addr;
      const auto code = uv_tcp_getpeername(native(), addr.native(), addr.native_len());
      if (code < 0) return result<socket_address>{make_error_code(code)};
      return result<socket_address>{addr};
    }

    void connect(connect_request &request, const ipv4 &addr, connect_request::callback callback) {
      ::uv::detail::submit_request(request, std::move(callback), [&] {
        return uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), &tcp::connect_trampoline);
      });
    }

    void connect(connect_request &request, const ipv6 &addr, connect_request::callback callback) {
      ::uv::detail::submit_request(request, std::move(callback), [&] {
        return uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), &tcp::connect_trampoline);
      });
    }

    template<auto Callback>
    void connect_static(connect_request &request, const ipv4 &addr) {
      throw_if_error(uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), [](uv_connect_t *raw, int status) noexcept {
        ::uv::detail::invoke_static_callback<Callback>(connect_request::from_native(raw), uv::status::from_native(status));
      }));
    }

    template<auto Callback>
    void connect_static(connect_request &request, const ipv6 &addr) {
      throw_if_error(uv_tcp_connect(request.native(), native(), addr.native_sockaddr(), [](uv_connect_t *raw, int status) noexcept {
        ::uv::detail::invoke_static_callback<Callback>(connect_request::from_native(raw), uv::status::from_native(status));
      }));
    }

#if UVPP_HAS_TCP_CLOSE_RESET
    status close_reset() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      const auto result = status::from_native(uv_tcp_close_reset(native(), &tcp::close_reset_trampoline));
      if (result) release_callbacks();
      return result;
    }

    status close_reset(close_callback cb) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      const auto result = status::from_native(uv_tcp_close_reset(native(), &tcp::close_reset_trampoline));
      if (result) {
        set_close_callback(std::move(cb));
        release_callbacks();
      }
      return result;
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), tcp&>
    status close_reset_static() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      const auto result = status::from_native(uv_tcp_close_reset(native(), [](uv_handle_t *raw) noexcept {
        auto &self = tcp::from_native(raw);
        self.finish_close();
        ::uv::detail::invoke_static_callback<Callback>(self);
      }));
      if (result) release_callbacks();
      return result;
    }
#endif

    status no_delay(bool enable) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return status::from_native(uv_tcp_nodelay(native(), enable ? 1 : 0));
    }

    status keep_alive(std::chrono::seconds delay) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      if (delay.count() <= 0 ||
          static_cast<std::uint64_t>(delay.count()) > (std::numeric_limits<unsigned int>::max)())
        return status::from_native(UV_EINVAL);
      return status::from_native(uv_tcp_keepalive(native(), 1,
                                                 static_cast<unsigned int>(delay.count())));
    }

    status disable_keep_alive() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return status::from_native(uv_tcp_keepalive(native(), 0, 0));
    }

    status simultaneous_accepts(bool enable) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return status::from_native(uv_tcp_simultaneous_accepts(native(), enable ? 1 : 0));
    }

  private:
    status finish_init(int code) noexcept {
      if (code < 0) {
        // libuv's TCP init_ex has already unlinked its failed registration and
        // closed any created socket. It leaves loop set: clear the stale sentinel
        // without unlinking again or starting asynchronous close.
        auto *data = native()->data;
        *native() = {};
        native()->data = data;
      }
      return status::from_native(code);
    }


    static void close_reset_trampoline(uv_handle_t *raw) noexcept {
      base::close_trampoline(raw);
    }

    static void connect_trampoline(uv_connect_t *raw, int status) noexcept {
      connect_request::from_native(raw).invoke(status);
    }
  };

}
