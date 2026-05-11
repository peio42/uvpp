#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/requests/request.hpp"

namespace uvpp::fs::raw {

  class request final : public basic_request<request, uv_fs_t> {
  public:
    using callback = std::function<void(request&)>;

    class cleanup_guard {
    public:
      explicit cleanup_guard(request &request) noexcept
        : request_{&request} {}

      cleanup_guard(const cleanup_guard&) = delete;
      cleanup_guard& operator=(const cleanup_guard&) = delete;

      cleanup_guard(cleanup_guard &&other) noexcept
        : request_{std::exchange(other.request_, nullptr)} {}

      cleanup_guard& operator=(cleanup_guard &&other) noexcept {
        if (this != &other) {
          cleanup();
          request_ = std::exchange(other.request_, nullptr);
        }

        return *this;
      }

      ~cleanup_guard() {
        cleanup();
      }

      void cleanup() noexcept {
        if (request_) {
          request_->cleanup();
          request_ = nullptr;
        }
      }

      void dismiss() noexcept {
        request_ = nullptr;
      }

    private:
      request *request_ = nullptr;
    };

    void set_callback(callback cb) {
      callback_ = std::move(cb);
    }

    void invoke() noexcept {
      auto callback = std::move(callback_);
      callback_ = {};

      if (callback) {
        detail::invoke_callback(callback, *this);
      }
    }

    void cleanup() noexcept {
      uv_fs_req_cleanup(native());
    }

    cleanup_guard scoped_cleanup() noexcept {
      return cleanup_guard{*this};
    }

    uv_fs_type type() const noexcept {
      return uv_fs_get_type(native());
    }

    ssize_t raw_result() const noexcept {
      return uv_fs_get_result(native());
    }

    int system_error() const noexcept {
      return uv_fs_get_system_error(native());
    }

    void *ptr() noexcept {
      return uv_fs_get_ptr(native());
    }

    const char *path() const noexcept {
      return uv_fs_get_path(native());
    }

    uv_stat_t *statbuf() noexcept {
      return uv_fs_get_statbuf(native());
    }

  private:
    callback callback_{};
  };
}
