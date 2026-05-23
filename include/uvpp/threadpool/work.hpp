#pragma once

#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/requests/work.hpp"

namespace uv {

  namespace detail {

    inline void work_trampoline(uv_work_t *raw) noexcept {
      work_request::from_native(raw).invoke_work();
    }

    inline void after_work_trampoline(uv_work_t *raw, int status) noexcept {
      work_request::from_native(raw).invoke_after(status);
    }

  }

  inline void queue_work(loop_view loop, work_request &request,
                         work_request::work_callback work,
                         work_request::after_callback after) {
    request.set_callbacks(std::move(work), std::move(after));

    try {
      throw_if_error(uv_queue_work(loop.native(), request.native(),
                                   detail::work_trampoline,
                                   detail::after_work_trampoline));
    } catch (...) {
      request.clear_callbacks();
      throw;
    }
  }

  inline void queue_work(loop &loop, work_request &request,
                         work_request::work_callback work,
                         work_request::after_callback after) {
    queue_work(loop.view(), request, std::move(work), std::move(after));
  }

  template<auto Work, auto After>
  void queue_work_static(loop_view loop, work_request &request) {
    throw_if_error(uv_queue_work(loop.native(), request.native(),
      [](uv_work_t *raw) noexcept {
        work_request::from_native(raw).template invoke_work_static<Work>();
      },
      [](uv_work_t *raw, int status) noexcept {
        work_request::from_native(raw).template invoke_after_static<After>(status);
      }));
  }

  template<auto Work, auto After>
  void queue_work_static(loop &loop, work_request &request) {
    queue_work_static<Work, After>(loop.view(), request);
  }

}
