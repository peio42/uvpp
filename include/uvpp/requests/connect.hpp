#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/requests/request.hpp"

namespace uvpp {

  class connect_request final : public basic_request<connect_request, uv_connect_t> {
  public:
    using callback = std::function<void(connect_request&, result)>;

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

  private:
    callback callback_{};
  };

}
