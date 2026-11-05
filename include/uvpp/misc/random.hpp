#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/requests/random.hpp"

namespace uv::raw {

#if UVPP_HAS_RANDOM

  inline status random_fill_now(std::span<std::byte> buffer) noexcept {
    return status::from_native(uv_random(nullptr, nullptr, buffer.data(), buffer.size(), 0, nullptr));
  }

  inline status random_fill(loop_view loop, random_request &request,
                            std::span<std::byte> buffer, random_request::callback callback) {
    return request.submit(loop, buffer, std::move(callback));
  }

  inline status random_fill(loop &loop, random_request &request,
                            std::span<std::byte> buffer, random_request::callback callback) {
    return random_fill(loop.view(), request, buffer, std::move(callback));
  }

  template<auto Callback>
    requires std::invocable<decltype(Callback), random_request&, random_result>
  status random_fill_static(loop_view loop, random_request &request, std::span<std::byte> buffer) noexcept {
    return request.template submit_static<Callback>(loop, buffer);
  }

  template<auto Callback>
    requires std::invocable<decltype(Callback), random_request&, random_result>
  status random_fill_static(loop &loop, random_request &request, std::span<std::byte> buffer) noexcept {
    return random_fill_static<Callback>(loop.view(), request, buffer);
  }

#endif

}
