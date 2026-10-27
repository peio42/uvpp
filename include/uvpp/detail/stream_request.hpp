#pragma once

#include <concepts>
#include <functional>
#include <utility>

#include "uvpp/core/callback.hpp"
#include "uvpp/requests/request.hpp"

namespace uv::detail {

  struct stream_request_submission;

  template<class Derived, class Native>
  class stream_request : public basic_request<Derived, Native> {
  public:
    using callback = std::function<void(Derived&, uv::status)>;

    bool pending() const noexcept { return pending_; }

    static void trampoline(Native *raw, int code) noexcept {
      auto &self = Derived::from_native(raw);
      auto &base = static_cast<stream_request&>(self);
      auto callback = std::move(base.callback_);
      base.callback_ = {};
      auto static_callback = std::exchange(base.static_callback_, nullptr);
      base.pending_ = false;
      // Nothing touches the request after delivery: user code may resubmit or
      // destroy its storage once this native completion has been delivered.
      if (static_callback) static_callback(self, uv::status::from_native(code));
      else if (callback) detail::invoke_callback(callback, self, uv::status::from_native(code));
    }

  private:
    friend struct stream_request_submission;
    callback callback_{};
    void (*static_callback_)(Derived&, uv::status) noexcept = nullptr;
    bool pending_ = false;
  };

  struct stream_request_submission {
    template<class Request, class Submit>
    static status submit(Request &request, typename Request::callback callback, Submit submit)
      noexcept(noexcept(submit())) {
      if (request.pending_) return status::from_native(UV_EBUSY);
      if (!callback) return status::from_native(UV_EINVAL);
      const auto result = status::from_native(submit());
      // These native operations never deliver completion inline. Commit only
      // after acceptance; native rejection or C++ preparation leaves no state.
      if (result) {
        request.callback_ = std::move(callback);
        request.static_callback_ = nullptr;
        request.pending_ = true;
      }
      return result;
    }

    template<auto Callback, class Request, class Submit>
      requires std::invocable<decltype(Callback), Request&, uv::status>
    static status submit_static(Request &request, Submit submit) noexcept(noexcept(submit())) {
      if (request.pending_) return status::from_native(UV_EBUSY);
      const auto result = status::from_native(submit());
      if (result) {
        request.callback_ = {};
        request.static_callback_ = [](Request &self, uv::status outcome) noexcept {
          detail::invoke_static_callback<Callback>(self, outcome);
        };
        request.pending_ = true;
      }
      return result;
    }
  };

}
