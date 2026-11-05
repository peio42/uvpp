#pragma once

#include <concepts>
#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/requests/request.hpp"

namespace uv::raw {

  class work_request final : public basic_request<work_request, uv_work_t> {
  public:
    using work_callback = std::function<void(work_request&)>;
    using after_callback = std::function<void(work_request&, uv::status)>;

    // Loop-thread only, including cancellation and submission.
    bool pending() const noexcept { return pending_; }
    uv::status cancel() noexcept {
      return pending_ ? uv::status::from_native(uv_cancel(native_request()))
                      : uv::status::from_native(UV_EINVAL);
    }

    uv::status submit(loop_view loop, work_callback work, after_callback after) {
      if (pending_) return uv::status::from_native(UV_EBUSY);
      if (!work || !after) return uv::status::from_native(UV_EINVAL);
      // A worker may run before uv_queue_work returns. Publish both callbacks
      // before native submission; on acceptance do not touch the worker slot.
      work_ = std::move(work);
      after_ = std::move(after);
      pending_ = true;
      const int rc = uv_queue_work(loop.native(), native(), &work_request::run,
                                   &work_request::complete);
      if (rc < 0) {
        pending_ = false;
        work_ = {};
        after_ = {};
      }
      return uv::status::from_native(rc);
    }

    template<auto Work, auto After>
      requires std::invocable<decltype(Work), work_request&> &&
               std::invocable<decltype(After), work_request&, uv::status>
    uv::status submit_static(loop_view loop) noexcept {
      if (pending_) return uv::status::from_native(UV_EBUSY);
      pending_ = true;
      const int rc = uv_queue_work(loop.native(), native(),
        [](uv_work_t *raw) noexcept {
          auto &self = from_native(raw);
          detail::invoke_static_callback<Work>(self);
        },
        [](uv_work_t *raw, int code) noexcept {
          auto &self = from_native(raw);
          self.pending_ = false;
          // No request access after delivery; the callback may resubmit or destroy it.
          detail::invoke_static_callback<After>(self, uv::status::from_native(code));
        });
      if (rc < 0) pending_ = false;
      return uv::status::from_native(rc);
    }

  private:
    static void run(uv_work_t *raw) noexcept {
      auto &self = from_native(raw);
      auto work = std::move(self.work_);
      self.work_ = {};
      detail::invoke_callback(work, self);
    }

    static void complete(uv_work_t *raw, int code) noexcept {
      auto &self = from_native(raw);
      // If cancelled before execution, the work callback remains in the slot.
      // Otherwise the worker has already finished with it before this callback.
      self.work_ = {};
      auto after = std::move(self.after_);
      self.after_ = {};
      self.pending_ = false;
      detail::invoke_callback(after, self, uv::status::from_native(code));
    }

    work_callback work_{};
    after_callback after_{};
    bool pending_ = false;
  };

}
