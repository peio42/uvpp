#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/requests/request.hpp"

namespace uv {

  class udp_send_request final : public basic_request<udp_send_request, uv_udp_send_t> {
  public:
    using callback = std::function<void(udp_send_request&, result)>;

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
