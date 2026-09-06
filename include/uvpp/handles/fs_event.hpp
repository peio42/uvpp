#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv {

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
    result status() const noexcept { return result{status_}; }
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

  class fs_event final : public basic_handle<fs_event, uv_fs_event_t> {
  public:
    using callback = std::function<void(fs_event&, fs_event_result)>;

    explicit fs_event(loop &l) {
      throw_if_error(uv_fs_event_init(l.native(), native()));
    }

    explicit fs_event(loop_view l) {
      throw_if_error(uv_fs_event_init(l.native(), native()));
    }

    void start(std::string_view path, unsigned int flags, callback cb) {
      callback_.replace(std::move(cb));
      std::string storage{path};
      throw_if_error(uv_fs_event_start(native(), &fs_event::event_trampoline, storage.c_str(), flags));
    }

    void start(std::string_view path, callback cb) {
      start(path, 0, std::move(cb));
    }

    void start(std::string_view path, fs_event_flag flag, callback cb) {
      start(path, static_cast<unsigned int>(flag), std::move(cb));
    }

    template<auto Callback>
    void start_static(std::string_view path, unsigned int flags = 0) {
      std::string storage{path};
      throw_if_error(uv_fs_event_start(native(), [](uv_fs_event_t *raw, const char *filename, int events, int status) noexcept {
        detail::invoke_static_callback<Callback>(fs_event::from_native(raw), fs_event_result{filename, events, status});
      }, storage.c_str(), flags));
    }

    template<auto Callback>
    void start_static(std::string_view path, fs_event_flag flag) {
      start_static<Callback>(path, static_cast<unsigned int>(flag));
    }

    void stop() {
      throw_if_error(uv_fs_event_stop(native()));
    }

    std::string path() {
      return get_path([this](char *buffer, std::size_t *size) {
        return uv_fs_event_getpath(native(), buffer, size);
      });
    }

  private:
    template<class Getter>
    static std::string get_path(Getter getter) {
      std::vector<char> buffer(256);

      for (;;) {
        auto size = buffer.size();
        auto status = getter(buffer.data(), &size);

        if (status == UV_ENOBUFS) {
          buffer.resize(size);
          continue;
        }

        throw_if_error(status);
        if (size > 0 && buffer[size - 1] == '\0') {
          --size;
        }

        return std::string{buffer.data(), size};
      }
    }

    static void event_trampoline(uv_fs_event_t *raw, const char *filename, int events, int status) noexcept {
      auto &self = fs_event::from_native(raw);
      self.callback_.invoke([&](callback &callback) {
        callback(self, fs_event_result{filename, events, status});
      });
    }

    detail::persistent_callback_slot<callback> callback_{};
  };

}
