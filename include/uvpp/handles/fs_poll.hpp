#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/fs/status.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv {

  class fs_poll_result {
  public:
    fs_poll_result(int status, const uv_stat_t *previous, const uv_stat_t *current) noexcept
      : status_{status}, previous_{previous}, current_{current} {}

    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    result status() const noexcept { return result{status_}; }

    const uv_stat_t *previous() const noexcept { return previous_; }
    const uv_stat_t *current() const noexcept { return current_; }

    fs::file_status previous_status() const noexcept {
      return previous_ ? fs::file_status{*previous_} : fs::file_status{};
    }

    fs::file_status current_status() const noexcept {
      return current_ ? fs::file_status{*current_} : fs::file_status{};
    }

  private:
    int status_ = 0;
    const uv_stat_t *previous_ = nullptr;
    const uv_stat_t *current_ = nullptr;
  };

  class fs_poll final : public basic_handle<fs_poll, uv_fs_poll_t> {
  public:
    using callback = std::function<void(fs_poll&, fs_poll_result)>;

    explicit fs_poll(loop &l) {
      throw_if_error(uv_fs_poll_init(l.native(), native()));
    }

    explicit fs_poll(loop_view l) {
      throw_if_error(uv_fs_poll_init(l.native(), native()));
    }

    template<class Rep, class Period>
    void start(std::string_view path, std::chrono::duration<Rep, Period> interval, callback cb) {
      callback_.replace(std::move(cb));
      std::string storage{path};
      throw_if_error(uv_fs_poll_start(native(), &fs_poll::poll_trampoline, storage.c_str(), millis(interval)));
    }

    template<auto Callback, class Rep, class Period>
    void start_static(std::string_view path, std::chrono::duration<Rep, Period> interval) {
      std::string storage{path};
      throw_if_error(uv_fs_poll_start(native(), [](uv_fs_poll_t *raw, int status, const uv_stat_t *previous, const uv_stat_t *current) noexcept {
        detail::invoke_static_callback<Callback>(fs_poll::from_native(raw), fs_poll_result{status, previous, current});
      }, storage.c_str(), millis(interval)));
    }

    void stop() {
      throw_if_error(uv_fs_poll_stop(native()));
    }

    std::string path() {
      return get_path([this](char *buffer, std::size_t *size) {
        return uv_fs_poll_getpath(native(), buffer, size);
      });
    }

  private:
    template<class Rep, class Period>
    static unsigned int millis(std::chrono::duration<Rep, Period> duration) {
      return static_cast<unsigned int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    }

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

    static void poll_trampoline(uv_fs_poll_t *raw, int status, const uv_stat_t *previous, const uv_stat_t *current) noexcept {
      auto &self = fs_poll::from_native(raw);
      self.callback_.invoke([&](callback &callback) {
        callback(self, fs_poll_result{status, previous, current});
      });
    }

    detail::persistent_callback_slot<callback> callback_{};
  };

}
