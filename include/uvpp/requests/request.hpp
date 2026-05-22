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

    // submit_request is suitable for requests whose submission inputs are
    // pointers or values owned by the caller (buffers, addresses). It sets the
    // callback, calls libuv, and rolls back the callback on immediate failure.
    //
    // Do NOT use submit_request when the request itself must store the
    // submission inputs and pass pointers to those stored copies into libuv
    // (e.g. getaddrinfo_request stores node/service strings and passes their
    // c_str() pointers). In that case set_inputs() must be called before the
    // libuv call, and clear_inputs() must be added to the catch rollback.
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

  }

}
