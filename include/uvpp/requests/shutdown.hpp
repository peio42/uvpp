#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/requests/request.hpp"

namespace uv {

  class shutdown_request final : public basic_request<shutdown_request, uv_shutdown_t> {
  public:
    using callback = std::function<void(shutdown_request&, uv::status)>;

    void set_callback(callback cb) {
      callback_ = std::move(cb);
    }

    void invoke(int status) noexcept {
      auto callback = std::move(callback_);
      callback_ = {};

      if (callback) {
        detail::invoke_callback(callback, *this, uv::status::from_native(status));
      }
    }

  private:
    callback callback_{};
  };

}
