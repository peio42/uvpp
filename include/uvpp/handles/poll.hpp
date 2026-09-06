#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv {

  enum class poll_event : int {
    readable = UV_READABLE,
    writable = UV_WRITABLE,
    disconnect = UV_DISCONNECT,
    prioritized = UV_PRIORITIZED
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

  class poll_result {
  public:
    poll_result(int status, int events) noexcept
      : status_{status}, events_{poll_events::from_raw(events)} {}

    bool ok() const noexcept { return status_.ok(); }
    explicit operator bool() const noexcept { return ok(); }
    result status() const noexcept { return status_; }
    std::error_code error_code() const noexcept { return status_.error_code(); }
    poll_events events() const noexcept { return events_; }
    bool has_event(poll_event event) const noexcept { return events_.has(event); }
    int raw_events() const noexcept { return events_.raw(); }

  private:
    result status_;
    poll_events events_;
  };

  struct socket_poll_t {
    explicit socket_poll_t() = default;
  };

  inline constexpr socket_poll_t socket_poll{};

  class poll final : public basic_handle<poll, uv_poll_t> {
  public:
    using callback = std::function<void(poll&, poll_result)>;

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

    void start(poll_events events, callback cb) {
      callback_.replace(std::move(cb));
      throw_if_error(uv_poll_start(native(), events.raw(), &poll::poll_trampoline));
    }

    void start(poll_event event, callback cb) {
      start(poll_events{event}, std::move(cb));
    }

    void start_raw(int events, callback cb) {
      start(poll_events::from_raw(events), std::move(cb));
    }

    template<auto Callback>
    void start_static(poll_events events) {
      throw_if_error(uv_poll_start(native(), events.raw(), [](uv_poll_t *raw, int status, int events) noexcept {
        detail::invoke_static_callback<Callback>(poll::from_native(raw), poll_result{status, events});
      }));
    }

    template<auto Callback>
    void start_static(poll_event event) {
      start_static<Callback>(poll_events{event});
    }

    template<auto Callback>
    void start_static_raw(int events) {
      start_static<Callback>(poll_events::from_raw(events));
    }

    void stop() {
      throw_if_error(uv_poll_stop(native()));
    }

  private:
    static void poll_trampoline(uv_poll_t *raw, int status, int events) noexcept {
      auto &self = poll::from_native(raw);
      self.callback_.invoke([&](callback &callback) {
        callback(self, poll_result{status, events});
      });
    }

    detail::persistent_callback_slot<callback> callback_{};
  };

}
