#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/requests/request.hpp"

namespace uv {

  class write_request final : public basic_request<write_request, uv_write_t> {
  public:
    using callback = std::function<void(write_request&, result)>;

    void set_callback(callback cb) {
      callback_ = std::move(cb);
    }

    void invoke(int status) noexcept {
      auto callback = std::move(callback_);
      callback_ = {};

      if (callback) {
        detail::invoke_callback(callback, *this, result{status});
      }
    }

    static void trampoline(uv_write_t *raw, int status) noexcept {
      from_native(raw).invoke(status);
    }

  private:
    callback callback_{};
  };

}
