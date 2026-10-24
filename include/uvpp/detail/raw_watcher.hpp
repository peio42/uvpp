#pragma once

#include <concepts>
#include <functional>
#include <utility>

#include "uvpp/core/callback.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv::detail {

  // Shared raw subscription bookkeeping. Native start/stop and payload policy
  // remain in each watcher. No native start calls back inline.
  template<class Derived, class Native, class... Args>
  class raw_watcher_handle : public basic_handle<Derived, Native> {
    using base = basic_handle<Derived, Native>;

  public:
    using callback = std::function<void(Derived&, Args...)>;
    using typename base::close_callback;

    void close() noexcept {
      base::close(); // uv_close quiesces native delivery before releasing captures.
      callback_.replace({});
    }

    void close(close_callback cb) noexcept {
      base::close(std::move(cb));
      callback_.replace({});
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), Derived&>
    void close_static() noexcept {
      base::template close_static<Callback>();
      callback_.replace({});
    }

    result<uv_os_fd_t> fileno() const noexcept {
      uv_os_fd_t fd{};
      const auto code = uv_fileno(const_cast<uv_handle_t *>(this->native_handle()), &fd);
      if (code < 0) return result<uv_os_fd_t>{make_error_code(code)};
      return result<uv_os_fd_t>{fd};
    }

    result<int> send_buffer_size() const noexcept {
      int size = 0;
      const auto code = uv_send_buffer_size(const_cast<uv_handle_t *>(this->native_handle()), &size);
      if (code < 0) return result<int>{make_error_code(code)};
      return result<int>{size};
    }

    status send_buffer_size(int size) noexcept {
      return status::from_native(uv_send_buffer_size(this->native_handle(), &size));
    }

    result<int> receive_buffer_size() const noexcept {
      int size = 0;
      const auto code = uv_recv_buffer_size(const_cast<uv_handle_t *>(this->native_handle()), &size);
      if (code < 0) return result<int>{make_error_code(code)};
      return result<int>{size};
    }

    status receive_buffer_size(int size) noexcept {
      return status::from_native(uv_recv_buffer_size(this->native_handle(), &size));
    }

  protected:
    status commit_start(int native_status, callback cb = {}) noexcept {
      // A failed native reconfiguration can itself stop the previous source.
      // Keep its callable only while native delivery remains active.
      if (native_status >= 0 || !this->active()) {
        callback_.replace(this->active() ? std::move(cb) : callback{});
      }
      return status::from_native(native_status);
    }

    status commit_stop(int native_status) noexcept {
      if (native_status >= 0) {
        callback_.replace({});
      }
      return status::from_native(native_status);
    }

    void deliver(Args... args) noexcept {
      callback_.invoke([&](callback &cb) {
        cb(static_cast<Derived&>(*this), std::forward<Args>(args)...);
      });
    }

    void deliver_terminal(Args... args) noexcept {
      auto cb = callback_.take();
      if (cb) {
        invoke_callback(cb, static_cast<Derived&>(*this), std::forward<Args>(args)...);
      }
    }

  private:
    persistent_callback_slot<callback> callback_{};
  };

}
