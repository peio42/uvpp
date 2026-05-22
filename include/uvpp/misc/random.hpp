#pragma once

#include <cstddef>
#include <span>
#include <system_error>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/requests/random.hpp"
#include "uvpp/requests/request.hpp"

namespace uv {

#if UVPP_HAS_RANDOM

  namespace detail {

    inline void random_trampoline(uv_random_t *raw, int status, void *buffer,
                                  std::size_t size) noexcept {
      random_request::from_native(raw).invoke(status, buffer, size);
    }

  }

  inline void random_fill(loop_view loop, random_request &request,
                          std::span<std::byte> buffer,
                          random_request::callback callback) {
    detail::submit_request(request, std::move(callback), [&] {
      return uv_random(loop.native(), request.native(), buffer.data(), buffer.size(), 0,
                       detail::random_trampoline);
    });
  }

  inline void random_fill(loop &loop, random_request &request,
                          std::span<std::byte> buffer,
                          random_request::callback callback) {
    random_fill(loop.view(), request, buffer, std::move(callback));
  }

  template<auto Callback>
  void random_fill_static(loop_view loop, random_request &request,
                          std::span<std::byte> buffer) {
    throw_if_error(uv_random(loop.native(), request.native(), buffer.data(), buffer.size(), 0,
      [](uv_random_t *raw, int status, void *callback_buffer, std::size_t callback_size) noexcept {
        random_request::from_native(raw).template invoke_static<Callback>(
          status, callback_buffer, callback_size);
      }));
  }

  template<auto Callback>
  void random_fill_static(loop &loop, random_request &request,
                          std::span<std::byte> buffer) {
    random_fill_static<Callback>(loop.view(), request, buffer);
  }

  inline void random_fill(std::span<std::byte> buffer) {
    throw_if_error(uv_random(nullptr, nullptr, buffer.data(), buffer.size(), 0, nullptr));
  }

  inline std::error_code try_random_fill(std::span<std::byte> buffer) noexcept {
    return make_error_code(uv_random(nullptr, nullptr, buffer.data(), buffer.size(), 0, nullptr));
  }

#endif

}
