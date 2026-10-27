#pragma once

#include "uvpp/detail/stream_request.hpp"

namespace uv::raw {

  class shutdown_request final : public detail::stream_request<shutdown_request, uv_shutdown_t> {};

}
