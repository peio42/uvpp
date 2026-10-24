#pragma once

#include <concepts>
#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/detail/raw_watcher.hpp"

namespace uv {

  enum class poll_event : int {
    readable = UV_READABLE,
    writable = UV_WRITABLE,
#if UVPP_HAS_POLL_DISCONNECT
    disconnect = UV_DISCONNECT,
#endif
#if UVPP_HAS_POLL_PRIORITIZED
    prioritized = UV_PRIORITIZED,
#endif
  };

  class poll_events {
  public:
    constexpr poll_events() noexcept = default;

    constexpr poll_events(poll_event event) noexcept
      : events_{static_cast<int>(event)} {}

    static constexpr poll_events from_raw(int events) noexcept {
      return poll_events{events, raw_tag{}};
    }

    constexpr int raw() const noexcept { return events_; }
    constexpr explicit operator bool() const noexcept { return events_ != 0; }

    constexpr bool has(poll_event event) const noexcept {
      return (events_ & static_cast<int>(event)) != 0;
    }

    constexpr poll_events &operator|=(poll_event event) noexcept {
      events_ |= static_cast<int>(event);
      return *this;
    }

    constexpr poll_events &operator|=(poll_events events) noexcept {
      events_ |= events.events_;
      return *this;
    }

  private:
    struct raw_tag {};

    constexpr poll_events(int events, raw_tag) noexcept
      : events_{events} {}

    int events_ = 0;
  };

  constexpr poll_events operator|(poll_event lhs, poll_event rhs) noexcept {
    auto events = poll_events{lhs};
    events |= rhs;
    return events;
  }

  constexpr poll_events operator|(poll_events lhs, poll_event rhs) noexcept {
    lhs |= rhs;
    return lhs;
  }

  constexpr poll_events operator|(poll_event lhs, poll_events rhs) noexcept {
    rhs |= lhs;
    return rhs;
  }

  constexpr poll_events operator|(poll_events lhs, poll_events rhs) noexcept {
    lhs |= rhs;
    return lhs;
  }

  constexpr bool has_poll_event(poll_events events, poll_event event) noexcept {
    return events.has(event);
  }

  struct socket_poll_t {
    explicit socket_poll_t() = default;
  };

  inline constexpr socket_poll_t socket_poll{};

}

namespace uv::raw {

  class poll_result {
  public:
    poll_result(int status, int events) noexcept
      : status_{uv::status::from_native(status)}, events_{poll_events::from_raw(events)} {}

    bool ok() const noexcept { return static_cast<bool>(status_); }
    explicit operator bool() const noexcept { return ok(); }
    uv::status status() const noexcept { return status_; }
    uv::error_code error_code() const noexcept { return status_.error(); }
    int raw_status() const noexcept { return status_.error().native(); }
    poll_events events() const noexcept { return events_; }
    bool has_event(poll_event event) const noexcept { return events_.has(event); }
    int raw_events() const noexcept { return events_.raw(); }

  private:
    uv::status status_;
    poll_events events_;
  };

  class poll final : public ::uv::detail::raw_watcher_handle<poll, uv_poll_t, poll_result> {
  public:
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

    status start(poll_events events, callback cb) {
      if (closing()) return status::from_native(UV_EBADF);
      if (!valid_events(events)) return status::from_native(UV_EINVAL);
      return commit_start(uv_poll_start(native(), events.raw(), &poll::poll_trampoline), std::move(cb));
    }

    status start(poll_event event, callback cb) {
      return start(poll_events{event}, std::move(cb));
    }

    status start_raw(int events, callback cb) {
      return start(poll_events::from_raw(events), std::move(cb));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), poll&, poll_result>
    status start_static(poll_events events) noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      if (!valid_events(events)) return status::from_native(UV_EINVAL);
      return commit_start(uv_poll_start(native(), events.raw(), [](uv_poll_t *raw, int status, int events) noexcept {
        auto &self = poll::from_native(raw);
        if (status < 0 && !self.stop()) std::terminate();
        ::uv::detail::invoke_static_callback<Callback>(self, poll_result{status, events});
      }));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), poll&, poll_result>
    status start_static(poll_event event) noexcept {
      return start_static<Callback>(poll_events{event});
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), poll&, poll_result>
    status start_static_raw(int events) noexcept {
      return start_static<Callback>(poll_events::from_raw(events));
    }

    status stop() noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      return commit_stop(uv_poll_stop(native()));
    }

  private:
    static bool valid_events(poll_events events) noexcept {
      int allowed = UV_READABLE | UV_WRITABLE;
#if UVPP_HAS_POLL_DISCONNECT
      allowed |= UV_DISCONNECT;
#endif
#if UVPP_HAS_POLL_PRIORITIZED
      allowed |= UV_PRIORITIZED;
#endif
      return (events.raw() & ~allowed) == 0;
    }

    static void poll_trampoline(uv_poll_t *raw, int status, int events) noexcept {
      auto &self = poll::from_native(raw);
      if (status < 0) {
        if (uv_poll_stop(raw) < 0) std::terminate();
        self.deliver_terminal(poll_result{status, events});
      } else {
        self.deliver(poll_result{status, events});
      }
    }
  };

}
