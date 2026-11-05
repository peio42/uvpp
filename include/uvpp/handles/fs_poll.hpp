#pragma once

#include <chrono>
#include <cmath>
#include <concepts>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/initializable_raw_watcher.hpp"
#include "uvpp/fs/status.hpp"

namespace uv::raw {

  class fs_poll_result {
  public:
    fs_poll_result(int status, const uv_stat_t *previous, const uv_stat_t *current) noexcept
      : status_{status}, previous_{previous}, current_{current} {}

    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    uv::status status() const noexcept { return uv::status::from_native(status_); }
    int raw_status() const noexcept { return status_ < 0 ? status_ : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }
    const uv_stat_t *previous() const noexcept { return previous_; }
    const uv_stat_t *current() const noexcept { return current_; }
    ::uv::fs::file_status previous_status() const noexcept {
      return previous_ ? ::uv::fs::file_status{*previous_} : ::uv::fs::file_status{};
    }
    ::uv::fs::file_status current_status() const noexcept {
      return current_ ? ::uv::fs::file_status{*current_} : ::uv::fs::file_status{};
    }

  private:
    int status_ = 0;
    const uv_stat_t *previous_ = nullptr;
    const uv_stat_t *current_ = nullptr;
  };

  class fs_poll final : public detail::initializable_raw_watcher_handle<fs_poll, uv_fs_poll_t, fs_poll_result> {
  public:
    fs_poll() noexcept = default;
    explicit fs_poll(loop &l) : fs_poll(l.view()) {}
    explicit fs_poll(loop_view l) { init(l).throw_if_error(); }

    status init(loop &l) noexcept { return init(l.view()); }
    status init(loop_view l) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      return status::from_native(uv_fs_poll_init(l.native(), native()));
    }

    template<class Rep, class Period>
      requires (std::integral<Rep> || std::floating_point<Rep>)
    status start(std::string_view path, std::chrono::duration<Rep, Period> interval, callback cb) {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      if (path.find('\0') != std::string_view::npos) return status::from_native(UV_EINVAL);
      const auto delay = millis(interval);
      if (!delay) return status{delay.error()};
      if (active()) {
        if (interval_ != delay.value()) return status::from_native(UV_EBUSY);
        const auto same_path = matches_path(path);
        if (!same_path) return status{same_path.error()};
        if (!same_path.value()) return status::from_native(UV_EBUSY);
        replace_callback(std::move(cb));
        static_callback_ = nullptr;
        return {};
      }
      std::string storage{path};
      const auto result = commit_start(uv_fs_poll_start(native(), &fs_poll::poll_trampoline,
                                                        storage.c_str(), delay.value()), std::move(cb));
      if (result) { interval_ = delay.value(); static_callback_ = nullptr; }
      return result;
    }

    template<auto Callback, class Rep, class Period>
      requires std::invocable<decltype(Callback), fs_poll&, fs_poll_result> &&
               (std::integral<Rep> || std::floating_point<Rep>)
    status start_static(std::string_view path, std::chrono::duration<Rep, Period> interval) {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      if (path.find('\0') != std::string_view::npos) return status::from_native(UV_EINVAL);
      const auto delay = millis(interval);
      if (!delay) return status{delay.error()};
      if (active()) {
        if (interval_ != delay.value()) return status::from_native(UV_EBUSY);
        const auto same_path = matches_path(path);
        if (!same_path) return status{same_path.error()};
        if (!same_path.value()) return status::from_native(UV_EBUSY);
        replace_callback({});
        static_callback_ = &static_delivery<Callback>;
        return {};
      }
      std::string storage{path};
      const auto result = commit_start(uv_fs_poll_start(native(), &fs_poll::poll_trampoline,
                                                        storage.c_str(), delay.value()));
      if (result) { interval_ = delay.value(); static_callback_ = &static_delivery<Callback>; }
      return result;
    }

    status stop() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      const auto result = commit_stop(uv_fs_poll_stop(native()));
      if (result) static_callback_ = nullptr;
      return result;
    }

    result<std::string> path() const {
      if (!initialized() || closing()) return result<std::string>{make_error_code(UV_EBADF)};
      std::vector<char> buffer(256);
      for (;;) {
        auto size = buffer.size();
        const int code = uv_fs_poll_getpath(const_cast<uv_fs_poll_t *>(native()), buffer.data(), &size);
        if (code == UV_ENOBUFS) { buffer.resize(size); continue; }
        if (code < 0) return result<std::string>{make_error_code(code)};
        if (size > 0 && buffer[size - 1] == '\0') --size;
        return result<std::string>{std::string{buffer.data(), size}};
      }
    }

  private:
    result<bool> matches_path(std::string_view path) const {
      // Prepare one writable path buffer; a longer native path is already a
      // mismatch, so no second allocation or native-context inspection is needed.
      std::string storage{path};
      auto size = storage.size() + 1;
      const int code = uv_fs_poll_getpath(const_cast<uv_fs_poll_t *>(native()), storage.data(), &size);
      if (code == UV_ENOBUFS) return result<bool>{false};
      if (code < 0) return result<bool>{make_error_code(code)};
      return result<bool>{size == path.size() && std::string_view{storage.data(), size} == path};
    }

    template<class Rep, class Period>
    static result<unsigned int> millis(std::chrono::duration<Rep, Period> duration) noexcept {
      const auto ms = std::chrono::duration<long double, std::milli>{duration}.count();
      // Native uses an unsigned millisecond count and treats zero as 1 ms.
      // Require a positive whole-millisecond result; truncate fractions toward zero.
      if (!std::isfinite(ms) || ms < 1 ||
          ms >= static_cast<long double>((std::numeric_limits<unsigned int>::max)()) + 1.0L)
        return result<unsigned int>{make_error_code(UV_EINVAL)};
      return result<unsigned int>{static_cast<unsigned int>(ms)};
    }

    template<auto Callback>
    static void static_delivery(fs_poll &self, fs_poll_result value) noexcept {
      detail::invoke_static_callback<Callback>(self, value);
    }

    static void poll_trampoline(uv_fs_poll_t *raw, int code,
                                const uv_stat_t *previous, const uv_stat_t *current) noexcept {
      auto &self = fs_poll::from_native(raw);
      const auto selected = self.static_callback_;
      if (selected) selected(self, fs_poll_result{code, previous, current});
      else self.deliver(fs_poll_result{code, previous, current});
    }

    unsigned int interval_ = 0;
    void (*static_callback_)(fs_poll&, fs_poll_result) noexcept = nullptr;
  };

}
