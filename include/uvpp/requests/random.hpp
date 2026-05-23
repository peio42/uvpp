#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <system_error>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/requests/request.hpp"

namespace uv {

#if UVPP_HAS_RANDOM

  class random_result {
  public:
    random_result() = default;
    random_result(int status, void *buffer, std::size_t size) noexcept
      : status_{status}, buffer_{static_cast<std::byte *>(buffer)}, size_{size} {}

    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    result status() const noexcept { return result{status_}; }
    int raw_status() const noexcept { return status_; }
    std::error_code error_code() const noexcept { return make_error_code(status_); }

    std::span<std::byte> bytes() const noexcept {
      if (!ok()) {
        return {};
      }

      return {buffer_, size_};
    }

  private:
    int status_ = 0;
    std::byte *buffer_ = nullptr;
    std::size_t size_ = 0;
  };

  class random_request final : public basic_request<random_request, uv_random_t> {
  public:
    using callback = std::function<void(random_request&, random_result)>;

    void set_callback(callback cb) {
      callback_ = std::move(cb);
    }

    void cancel() {
      throw_if_error(uv_cancel(native_request()));
    }

    std::error_code try_cancel() noexcept {
      return make_error_code(uv_cancel(native_request()));
    }

    void invoke(int status, void *buffer, std::size_t size) noexcept {
      auto callback = std::move(callback_);
      callback_ = {};

      if (callback) {
        detail::invoke_callback(callback, *this, random_result{status, buffer, size});
      }
    }

    template<auto Callback>
    void invoke_static(int status, void *buffer, std::size_t size) noexcept {
      detail::invoke_static_callback<Callback>(*this, random_result{status, buffer, size});
    }

  private:
    callback callback_{};
  };

#endif

}
