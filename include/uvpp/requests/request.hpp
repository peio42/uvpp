#pragma once

#include <uv.h>

#include "uvpp/core/native.hpp"

namespace uvpp {

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

  protected:
  private:
  };

}
