#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <span>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/requests/request.hpp"

namespace uv::raw {

#if UVPP_HAS_RANDOM

  class random_result {
  public:
    random_result() = default;
    random_result(int status, void *buffer, std::size_t size) noexcept
      : status_{status}, buffer_{static_cast<std::byte *>(buffer)}, size_{size} {}

    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    uv::status status() const noexcept { return uv::status::from_native(status_); }
    int raw_status() const noexcept { return status_; }
    uv::error_code error_code() const noexcept { return make_error_code(status_); }

    // Both spans borrow caller-owned storage. Contents after failure are undefined.
    std::span<std::byte> buffer() const noexcept { return {buffer_, size_}; }
    std::span<std::byte> bytes() const noexcept { return ok() ? buffer() : std::span<std::byte>{}; }

  private:
    int status_ = 0;
    std::byte *buffer_ = nullptr;
    std::size_t size_ = 0;
  };

  class random_request final : public basic_request<random_request, uv_random_t> {
  public:
    using callback = std::function<void(random_request&, random_result)>;

    bool pending() const noexcept { return pending_; }
    uv::status cancel() noexcept {
      return pending_ ? uv::status::from_native(uv_cancel(native_request()))
                      : uv::status::from_native(UV_EINVAL);
    }

    uv::status submit(loop_view loop, std::span<std::byte> buffer, callback cb) {
      if (pending_) return uv::status::from_native(UV_EBUSY);
      if (!cb) return uv::status::from_native(UV_EINVAL);
      const int rc = uv_random(loop.native(), native(), buffer.data(), buffer.size(), 0, &complete);
      // Completion is delivered on the loop, never inline during submission.
      if (rc >= 0) { callback_ = std::move(cb); static_callback_ = nullptr; pending_ = true; }
      return uv::status::from_native(rc);
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), random_request&, random_result>
    uv::status submit_static(loop_view loop, std::span<std::byte> buffer) noexcept {
      if (pending_) return uv::status::from_native(UV_EBUSY);
      const int rc = uv_random(loop.native(), native(), buffer.data(), buffer.size(), 0, &complete);
      if (rc >= 0) {
        callback_ = {};
        static_callback_ = [](random_request &request, random_result result) noexcept {
          detail::invoke_static_callback<Callback>(request, result);
        };
        pending_ = true;
      }
      return uv::status::from_native(rc);
    }

  private:
    static void complete(uv_random_t *raw, int code, void *buffer, std::size_t size) noexcept {
      auto &self = from_native(raw);
      auto callback = std::move(self.callback_);
      self.callback_ = {};
      auto static_callback = std::exchange(self.static_callback_, nullptr);
      self.pending_ = false;
      const random_result result{code, buffer, size};
      // Do not access the request after delivery: it may be reused or destroyed.
      if (static_callback) static_callback(self, result);
      else if (callback) detail::invoke_callback(callback, self, result);
    }

    callback callback_{};
    void (*static_callback_)(random_request&, random_result) noexcept = nullptr;
    bool pending_ = false;
  };

#endif

}
