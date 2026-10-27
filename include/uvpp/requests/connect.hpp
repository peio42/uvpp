#pragma once

#include "uvpp/detail/stream_request.hpp"

namespace uv::raw {

  class connect_request final : public detail::stream_request<connect_request, uv_connect_t> {};

}
