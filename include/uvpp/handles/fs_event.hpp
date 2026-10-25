#pragma once

#include <concepts>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/initializable_raw_watcher.hpp"

namespace uv::raw {

  enum class fs_event_kind : int {
    rename = UV_RENAME,
    change = UV_CHANGE
  };

  constexpr int operator|(fs_event_kind lhs, fs_event_kind rhs) noexcept {
    return static_cast<int>(lhs) | static_cast<int>(rhs);
  }

  constexpr int operator|(int lhs, fs_event_kind rhs) noexcept {
    return lhs | static_cast<int>(rhs);
  }

  constexpr bool has_fs_event(int events, fs_event_kind event) noexcept {
    return (events & static_cast<int>(event)) != 0;
  }

  enum class fs_event_flag : unsigned int {
    watch_entry = UV_FS_EVENT_WATCH_ENTRY,
    stat = UV_FS_EVENT_STAT,
    recursive = UV_FS_EVENT_RECURSIVE
  };

  constexpr unsigned int operator|(fs_event_flag lhs, fs_event_flag rhs) noexcept {
    return static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs);
  }

  constexpr unsigned int operator|(unsigned int lhs, fs_event_flag rhs) noexcept {
    return lhs | static_cast<unsigned int>(rhs);
  }

  class fs_event_result {
  public:
    fs_event_result(const char *filename, int events, int status) noexcept
      : filename_{filename ? std::string_view{filename} : std::string_view{}},
        events_{events},
        status_{status} {}

    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    uv::status status() const noexcept { return uv::status::from_native(status_); }
    int raw_status() const noexcept { return status_ < 0 ? status_ : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }
    std::string_view filename() const noexcept { return filename_; }
    int events() const noexcept { return events_; }

    bool has(fs_event_kind event) const noexcept {
      return has_fs_event(events_, event);
    }

  private:
    std::string_view filename_;
    int events_ = 0;
    int status_ = 0;
  };

  class fs_event final : public ::uv::detail::initializable_raw_watcher_handle<fs_event, uv_fs_event_t, fs_event_result> {
  public:
    fs_event() noexcept = default;

    explicit fs_event(loop &l) : fs_event(l.view()) {}

    explicit fs_event(loop_view l) {
      init(l).value();
    }

    status init(loop &l) noexcept { return init(l.view()); }

    status init(loop_view l) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      return status::from_native(uv_fs_event_init(l.native(), native()));
    }

    status start(std::string_view path, unsigned int flags, callback cb) {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      if (path.find('\0') != std::string_view::npos) return status::from_native(UV_EINVAL);
      std::string storage{path};
      return commit_start(uv_fs_event_start(native(), &fs_event::event_trampoline, storage.c_str(), flags),
                          std::move(cb));
    }

    status start(std::string_view path, callback cb) {
      return start(path, 0, std::move(cb));
    }

    status start(std::string_view path, fs_event_flag flag, callback cb) {
      return start(path, static_cast<unsigned int>(flag), std::move(cb));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), fs_event&, fs_event_result>
    status start_static(std::string_view path, unsigned int flags = 0) {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      if (path.find('\0') != std::string_view::npos) return status::from_native(UV_EINVAL);
      std::string storage{path};
      return commit_start(uv_fs_event_start(native(), [](uv_fs_event_t *raw, const char *filename, int events, int status) noexcept {
        auto &self = fs_event::from_native(raw);
        if (status < 0 && !self.stop()) std::terminate();
        ::uv::detail::invoke_static_callback<Callback>(self, fs_event_result{filename, events, status});
      }, storage.c_str(), flags));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), fs_event&, fs_event_result>
    status start_static(std::string_view path, fs_event_flag flag) {
      return start_static<Callback>(path, static_cast<unsigned int>(flag));
    }

    status stop() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      return commit_stop(uv_fs_event_stop(native()));
    }

    result<std::string> path() {
      if (!initialized() || closing()) return result<std::string>{make_error_code(UV_EBADF)};
      return get_path([this](char *buffer, std::size_t *size) {
        return uv_fs_event_getpath(native(), buffer, size);
      });
    }

  private:
    template<class Getter>
    static result<std::string> get_path(Getter getter) {
      std::vector<char> buffer(256);

      for (;;) {
        auto size = buffer.size();
        auto status = getter(buffer.data(), &size);

        if (status == UV_ENOBUFS) {
          buffer.resize(size);
          continue;
        }

        if (status < 0) return result<std::string>{make_error_code(status)};
        if (size > 0 && buffer[size - 1] == '\0') {
          --size;
        }

        return result<std::string>{std::string{buffer.data(), size}};
      }
    }

    static void event_trampoline(uv_fs_event_t *raw, const char *filename, int events, int status) noexcept {
      auto &self = fs_event::from_native(raw);
      if (status < 0) {
        if (uv_fs_event_stop(raw) < 0) std::terminate();
        self.deliver_terminal(fs_event_result{filename, events, status});
      } else {
        self.deliver(fs_event_result{filename, events, status});
      }
    }

  };

}
