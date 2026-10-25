#pragma once

#include "uvpp/detail/stream_request.hpp"

namespace uv::raw {

  class write_request final : public detail::stream_request<write_request, uv_write_t> {};

}
