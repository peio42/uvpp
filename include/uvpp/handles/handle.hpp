#pragma once

#include <functional>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/native.hpp"

namespace uv {

  template<class Derived, class Raw>
  class basic_handle : public detail::native_storage<Derived, Raw, uv_handle_t> {
  public:
    using raw_type = Raw;
    using close_callback = std::function<void(Derived&)>;
    using native_storage = detail::native_storage<Derived, Raw, uv_handle_t>;

    basic_handle(const basic_handle&) = delete;
    basic_handle& operator=(const basic_handle&) = delete;
    basic_handle(basic_handle&&) = delete;
    basic_handle& operator=(basic_handle&&) = delete;

    uv_handle_t *native_handle() noexcept {
      return this->native_base();
    }

    const uv_handle_t *native_handle() const noexcept {
      return this->native_base();
    }

    bool active() const noexcept {
      return uv_is_active(const_cast<uv_handle_t *>(this->native_handle())) != 0;
    }

    bool closing() const noexcept {
      return uv_is_closing(const_cast<uv_handle_t *>(this->native_handle())) != 0;
    }

    void close() noexcept {
      uv_close(this->native_handle(), nullptr);
    }

    void close(close_callback callback) noexcept {
      close_callback_ = std::move(callback);
      uv_close(this->native_handle(), &basic_handle::close_trampoline);
    }

    template<auto Callback>
    void close_static() noexcept {
      uv_close(this->native_handle(), [](uv_handle_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(native_storage::from_native(raw));
      });
    }

    void ref() noexcept {
      uv_ref(this->native_handle());
    }

    void unref() noexcept {
      uv_unref(this->native_handle());
    }

    bool has_ref() const noexcept {
      return uv_has_ref(const_cast<uv_handle_t *>(this->native_handle())) != 0;
    }

    int send_buffer_size() const {
      int value = 0;
      throw_if_error(uv_send_buffer_size(const_cast<uv_handle_t *>(this->native_handle()), &value));
      return value;
    }

    void send_buffer_size(int value) {
      throw_if_error(uv_send_buffer_size(this->native_handle(), &value));
    }

    int receive_buffer_size() const {
      int value = 0;
      throw_if_error(uv_recv_buffer_size(const_cast<uv_handle_t *>(this->native_handle()), &value));
      return value;
    }

    void receive_buffer_size(int value) {
      throw_if_error(uv_recv_buffer_size(this->native_handle(), &value));
    }

    uv_os_fd_t fileno() const {
      uv_os_fd_t fd{};
      throw_if_error(uv_fileno(this->native_handle(), &fd));
      return fd;
    }

  protected:
    basic_handle() = default;
    ~basic_handle() = default;

  private:
    static void close_trampoline(uv_handle_t *raw) noexcept {
      auto &self = native_storage::from_native(raw);
      auto &base = static_cast<basic_handle<Derived, Raw>&>(self);

      auto cb = std::move(base.close_callback_);
      if (cb) {
        detail::invoke_callback(cb, self);
      }
    }

    close_callback close_callback_{};
  };

}
