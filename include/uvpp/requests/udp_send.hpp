#pragma once

#include "uvpp/detail/stream_request.hpp"

namespace uv::raw {

  class udp_send_request final : public detail::stream_request<udp_send_request, uv_udp_send_t> {};

}
