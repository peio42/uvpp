#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uvpp {

  enum class poll_event : int {
    readable = UV_READABLE,
    writable = UV_WRITABLE,
    disconnect = UV_DISCONNECT,
    prioritized = UV_PRIORITIZED
  };

  constexpr int operator|(poll_event lhs, poll_event rhs) noexcept {
    return static_cast<int>(lhs) | static_cast<int>(rhs);
  }

  constexpr int operator|(int lhs, poll_event rhs) noexcept {
    return lhs | static_cast<int>(rhs);
  }

  constexpr bool has_poll_event(int events, poll_event event) noexcept {
    return (events & static_cast<int>(event)) != 0;
  }

  struct socket_poll_t {
    explicit socket_poll_t() = default;
  };

  inline constexpr socket_poll_t socket_poll{};

  class poll final : public basic_handle<poll, uv_poll_t> {
  public:
    using callback = std::function<void(poll&, result, int)>;

    poll(loop &l, int fd) {
      throw_if_error(uv_poll_init(l.native(), native(), fd));
    }

    poll(loop_view l, int fd) {
      throw_if_error(uv_poll_init(l.native(), native(), fd));
    }

    poll(loop &l, uv_os_sock_t socket, socket_poll_t) {
      throw_if_error(uv_poll_init_socket(l.native(), native(), socket));
    }

    poll(loop_view l, uv_os_sock_t socket, socket_poll_t) {
      throw_if_error(uv_poll_init_socket(l.native(), native(), socket));
    }

    void start(int events, callback cb) {
      callback_ = std::move(cb);
      throw_if_error(uv_poll_start(native(), events, &poll::poll_trampoline));
    }

    void start(poll_event event, callback cb) {
      start(static_cast<int>(event), std::move(cb));
    }

    template<auto Callback>
    void start_static(int events) {
      throw_if_error(uv_poll_start(native(), events, [](uv_poll_t *raw, int status, int events) noexcept {
        detail::invoke_static_callback<Callback>(poll::from_native(raw), result{status}, events);
      }));
    }

    template<auto Callback>
    void start_static(poll_event event) {
      start_static<Callback>(static_cast<int>(event));
    }

    void stop() {
      throw_if_error(uv_poll_stop(native()));
    }

  private:
    static void poll_trampoline(uv_poll_t *raw, int status, int events) noexcept {
      auto &self = poll::from_native(raw);
      if (self.callback_) {
        detail::invoke_callback(self.callback_, self, result{status}, events);
      }
    }

    callback callback_{};
  };

}
