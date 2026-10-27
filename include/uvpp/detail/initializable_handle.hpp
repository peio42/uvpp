#pragma once

#include <concepts>
#include <utility>

#include "uvpp/handles/handle.hpp"

namespace uv::detail {

  // Shared lifecycle guards; the concrete handle defines native initialization
  // and any family-specific callback quiescence.
  template<class Derived, class Native, class Base>
  class initializable_handle : public Base {
    using base = Base;

  public:
    using typename base::close_callback;

    // The native loop pointer records successful initialization, including
    // during/after close. It is never reset; native storage reuse is unsupported.
    bool initialized() const noexcept {
      return this->native_handle()->loop != nullptr;
    }

    bool active() const noexcept { return initialized() && base::active(); }
    bool closing() const noexcept { return initialized() && base::closing(); }
    bool has_ref() const noexcept { return initialized() && base::has_ref(); }

    void ref() noexcept { if (initialized()) base::ref(); }
    void unref() noexcept { if (initialized()) base::unref(); }

    void close() noexcept { if (initialized()) base::close(); }

    void close(close_callback cb) noexcept {
      if (initialized()) base::close(std::move(cb));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), Derived&>
    void close_static() noexcept {
      if (initialized()) base::template close_static<Callback>();
    }

    result<uv_os_fd_t> fileno() const noexcept {
      if (!initialized()) return result<uv_os_fd_t>{make_error_code(UV_EBADF)};
      uv_os_fd_t fd{};
      const auto code = uv_fileno(this->native_handle(), &fd);
      if (code < 0) return result<uv_os_fd_t>{make_error_code(code)};
      return result<uv_os_fd_t>{fd};
    }

    result<int> send_buffer_size() const noexcept {
      if (!initialized()) return result<int>{make_error_code(UV_EBADF)};
      int size = 0;
      const auto code = uv_send_buffer_size(const_cast<uv_handle_t *>(this->native_handle()), &size);
      if (code < 0) return result<int>{make_error_code(code)};
      return result<int>{size};
    }

    status send_buffer_size(int size) noexcept {
      if (!initialized()) return status::from_native(UV_EBADF);
      return status::from_native(uv_send_buffer_size(this->native_handle(), &size));
    }

    result<int> receive_buffer_size() const noexcept {
      if (!initialized()) return result<int>{make_error_code(UV_EBADF)};
      int size = 0;
      const auto code = uv_recv_buffer_size(const_cast<uv_handle_t *>(this->native_handle()), &size);
      if (code < 0) return result<int>{make_error_code(code)};
      return result<int>{size};
    }

    status receive_buffer_size(int size) noexcept {
      if (!initialized()) return status::from_native(UV_EBADF);
      return status::from_native(uv_recv_buffer_size(this->native_handle(), &size));
    }

  };

}
