#pragma once

#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/native.hpp"
#include "uvpp/core/types.hpp"

namespace uv {

  template<class Derived, class Raw>
  class basic_request : public detail::native_storage<Derived, Raw, uv_req_t> {
  public:
    using raw_type = Raw;
    using native_storage = detail::native_storage<Derived, Raw, uv_req_t>;

    basic_request(const basic_request&) = delete;
    basic_request& operator=(const basic_request&) = delete;
    basic_request(basic_request&&) = delete;
    basic_request& operator=(basic_request&&) = delete;

    basic_request() = default;
    ~basic_request() = default;

    uv_req_t *native_request() noexcept {
      return this->native_base();
    }

    const uv_req_t *native_request() const noexcept {
      return this->native_base();
    }

    request_type type() const noexcept {
      return static_cast<request_type>(this->native_request()->type);
    }

  protected:
  private:
  };

  namespace detail {

    // Use when the request stores only its callback before submission (all
    // submission inputs are caller-owned borrows: buffer views, addresses, …).
    template<class Request, class Callback, class Submit>
    void submit_request(Request &request, Callback callback, Submit submit) {
      request.set_callback(std::move(callback));

      try {
        throw_if_error(submit());
      } catch (...) {
        request.set_callback({});
        throw;
      }
    }

    // Use when the request also stores submission inputs (e.g. string copies
    // needed for null-termination). Caller must call set_inputs() before this
    // helper; on_error() is invoked — alongside the callback rollback — if
    // libuv rejects the submission immediately.
    template<class Request, class Callback, class Submit, class OnError>
    void submit_request(Request &request, Callback callback, Submit submit, OnError on_error) {
      request.set_callback(std::move(callback));

      try {
        throw_if_error(submit());
      } catch (...) {
        request.set_callback({});
        on_error();
        throw;
      }
    }

    // Use for static-callback paths where no runtime callback is stored.
    // Caller must call set_inputs() before this helper; on_error() is invoked
    // if libuv rejects the submission immediately.
    template<class Submit, class OnError>
    void submit_with_rollback(Submit submit, OnError on_error) {
      try {
        throw_if_error(submit());
      } catch (...) {
        on_error();
        throw;
      }
    }

  }

}
