#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/requests/request.hpp"

namespace uvpp {

  class write_request final : public basic_request<write_request, uv_write_t> {
  public:
    using callback = std::function<void(write_request&, result)>;

    void set_callback(callback cb) {
      callback_ = std::move(cb);
    }

    void invoke(int status) noexcept {
      if (callback_) {
        detail::invoke_callback(callback_, *this, result{status});
      }
    }

  private:
    callback callback_{};
  };

}
