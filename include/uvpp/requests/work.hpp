#pragma once

#include <functional>
#include <system_error>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/requests/request.hpp"

namespace uv {

  class work_request final : public basic_request<work_request, uv_work_t> {
  public:
    using work_callback = std::function<void(work_request&)>;
    using after_callback = std::function<void(work_request&, result)>;

    void set_callbacks(work_callback work, after_callback after) {
      work_callback_ = std::move(work);
      after_callback_ = std::move(after);
    }

    void clear_callbacks() noexcept {
      work_callback_ = {};
      after_callback_ = {};
    }

    void cancel() {
      throw_if_error(uv_cancel(native_request()));
    }

    std::error_code try_cancel() noexcept {
      return make_error_code(uv_cancel(native_request()));
    }

    void invoke_work() noexcept {
      auto callback = std::move(work_callback_);
      work_callback_ = {};

      if (callback) {
        detail::invoke_callback(callback, *this);
      }
    }

    void invoke_after(int status) noexcept {
      auto callback = std::move(after_callback_);
      after_callback_ = {};
      work_callback_ = {};

      if (callback) {
        detail::invoke_callback(callback, *this, result{status});
      }
    }

    template<auto Callback>
    void invoke_work_static() noexcept {
      detail::invoke_static_callback<Callback>(*this);
    }

    template<auto Callback>
    void invoke_after_static(int status) noexcept {
      detail::invoke_static_callback<Callback>(*this, result{status});
    }

  private:
    work_callback work_callback_{};
    after_callback after_callback_{};
  };

}
