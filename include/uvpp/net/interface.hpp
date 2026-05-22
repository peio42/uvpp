#pragma once

#include <string>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/version.hpp"

namespace uv {

  using interface_index = unsigned int;

#if UVPP_HAS_IF_INDEX_TO_NAME
  namespace detail {

    template<class Lookup>
    std::string interface_string(interface_index index, Lookup lookup) {
      std::string buffer(UV_IF_NAMESIZE, '\0');
      auto size = buffer.size();
      auto status = lookup(index, buffer.data(), &size);

      if (status == UV_ENOBUFS) {
        buffer.assign(size, '\0');
        status = lookup(index, buffer.data(), &size);
      }

      throw_if_error(status);
      buffer.resize(size);
      return buffer;
    }

  }

  inline std::string interface_name(interface_index index) {
    return detail::interface_string(index, uv_if_indextoname);
  }
#endif

#if UVPP_HAS_IF_INDEX_TO_IID
  inline std::string interface_identifier(interface_index index) {
    return detail::interface_string(index, uv_if_indextoiid);
  }
#endif

}
