#pragma once

#include "uvpp/detail/raw_watcher.hpp"

namespace uv::detail {

  // Opt-in lifecycle guards for raw watchers supporting explicit initialization.
  // Native init and subscription operations remain in the concrete handle.
  template<class Derived, class Native>
  class initializable_raw_watcher_handle : public raw_watcher_handle<Derived, Native> {
    using base = raw_watcher_handle<Derived, Native>;

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
      return base::fileno();
    }

    result<int> send_buffer_size() const noexcept {
      if (!initialized()) return result<int>{make_error_code(UV_EBADF)};
      return base::send_buffer_size();
    }

    status send_buffer_size(int size) noexcept {
      if (!initialized()) return status::from_native(UV_EBADF);
      return base::send_buffer_size(size);
    }

    result<int> receive_buffer_size() const noexcept {
      if (!initialized()) return result<int>{make_error_code(UV_EBADF)};
      return base::receive_buffer_size();
    }

    status receive_buffer_size(int size) noexcept {
      if (!initialized()) return status::from_native(UV_EBADF);
      return base::receive_buffer_size(size);
    }

  };

}
