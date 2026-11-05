#pragma once

#include <concepts>
#include <utility>

#include "uvpp/core/loop.hpp"
#include "uvpp/requests/work.hpp"

namespace uv::raw {

  inline status queue_work(loop_view loop, work_request &request,
                           work_request::work_callback work,
                           work_request::after_callback after) {
    return request.submit(loop, std::move(work), std::move(after));
  }

  inline status queue_work(loop &loop, work_request &request,
                           work_request::work_callback work,
                           work_request::after_callback after) {
    return queue_work(loop.view(), request, std::move(work), std::move(after));
  }

  template<auto Work, auto After>
    requires std::invocable<decltype(Work), work_request&> &&
             std::invocable<decltype(After), work_request&, uv::status>
  status queue_work_static(loop_view loop, work_request &request) noexcept {
    return request.template submit_static<Work, After>(loop);
  }

  template<auto Work, auto After>
    requires std::invocable<decltype(Work), work_request&> &&
             std::invocable<decltype(After), work_request&, uv::status>
  status queue_work_static(loop &loop, work_request &request) noexcept {
    return queue_work_static<Work, After>(loop.view(), request);
  }

}
