#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/handles/handle.hpp"
#include "uvpp/net/buffer.hpp"
#include "uvpp/requests/shutdown.hpp"
#include "uvpp/requests/write.hpp"

namespace uvpp {

  class read_result {
  public:
    read_result(ssize_t nread, const uv_buf_t *buf) noexcept
      : nread_{nread}, buffer_{buf ? buffer_view::from_native(*buf) : buffer_view{}} {}

    bool ok() const noexcept { return nread_ >= 0; }
    bool eof() const noexcept { return nread_ == UV_EOF; }
    ssize_t count() const noexcept { return nread_; }
    result status() const noexcept { return result{static_cast<int>(nread_)}; }

    std::span<const std::byte> bytes() const noexcept {
      if (nread_ <= 0) {
        return {};
      }

      return std::as_bytes(std::span{buffer_.data(), static_cast<std::size_t>(nread_)});
    }

    std::span<char> storage() const noexcept { return buffer_.chars(); }
    const buffer_view &raw_buffer() const noexcept { return buffer_; }

  private:
    ssize_t nread_;
    buffer_view buffer_;
  };

  template<class Derived, class Raw>
  class stream : public basic_handle<Derived, Raw> {
  public:
    using allocate_callback = std::function<buffer_view(Derived&, std::size_t)>;
    using read_callback = std::function<void(Derived&, read_result)>;
    using connection_callback = std::function<void(Derived&, result)>;

    uv_stream_t *native_stream() noexcept {
      return reinterpret_cast<uv_stream_t *>(this->native());
    }

    const uv_stream_t *native_stream() const noexcept {
      return reinterpret_cast<const uv_stream_t *>(this->native());
    }

    template<class Client>
    void accept(Client &client) {
      throw_if_error(uv_accept(native_stream(), client.native_stream()));
    }

    void listen(int backlog, connection_callback callback) {
      connection_callback_ = std::move(callback);
      throw_if_error(uv_listen(native_stream(), backlog, &stream::connection_trampoline));
    }

    void listen(connection_callback callback) {
      listen(64, std::move(callback));
    }

    template<auto Callback>
    void listen_static(int backlog = 64) {
      throw_if_error(uv_listen(native_stream(), backlog, [](uv_stream_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(Derived::from_native(reinterpret_cast<Raw *>(raw)), result{status});
      }));
    }

    void read_start(allocate_callback allocator, read_callback reader) {
      allocate_callback_ = std::move(allocator);
      read_callback_ = std::move(reader);
      throw_if_error(uv_read_start(native_stream(), &stream::alloc_trampoline, &stream::read_trampoline));
    }

    void read_stop() {
      throw_if_error(uv_read_stop(native_stream()));
    }

    void shutdown(shutdown_request &request, shutdown_request::callback callback) {
      request.set_callback(std::move(callback));
      throw_if_error(uv_shutdown(request.native(), native_stream(), &stream::shutdown_trampoline));
    }

    template<auto Callback>
    void shutdown_static(shutdown_request &request) {
      throw_if_error(uv_shutdown(request.native(), native_stream(), [](uv_shutdown_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(shutdown_request::from_native(raw), result{status});
      }));
    }

    void write(write_request &request, std::span<const buffer_view> buffers, write_request::callback callback) {
      request.set_callback(std::move(callback));
      throw_if_error(uv_write(request.native(), native_stream(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                     static_cast<unsigned int>(buffers.size()), &stream::write_trampoline));
    }

    void write(write_request &request, const buffer_view &buf, write_request::callback callback) {
      write(request, std::span<const buffer_view>{&buf, 1}, std::move(callback));
    }

    void write(write_request &request, std::span<const std::byte> bytes, write_request::callback callback) {
      auto raw = uv_buf_init(const_cast<char *>(reinterpret_cast<const char *>(bytes.data())),
                             static_cast<unsigned int>(bytes.size()));
      request.set_callback(std::move(callback));
      throw_if_error(uv_write(request.native(), native_stream(), &raw, 1, &stream::write_trampoline));
    }

    template<auto Callback>
    void write_static(write_request &request, std::span<const buffer_view> buffers) {
      throw_if_error(uv_write(request.native(), native_stream(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                     static_cast<unsigned int>(buffers.size()), [](uv_write_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(write_request::from_native(raw), result{status});
      }));
    }

  private:
    static void connection_trampoline(uv_stream_t *raw, int status) noexcept {
      auto &self = Derived::from_native(reinterpret_cast<Raw *>(raw));
      auto &base = static_cast<stream<Derived, Raw>&>(self);

      if (base.connection_callback_) {
        detail::invoke_callback(base.connection_callback_, self, result{status});
      }
    }

    static void alloc_trampoline(uv_handle_t *raw, size_t suggested_size, uv_buf_t *buf) noexcept {
      auto &self = Derived::from_native(raw);
      auto &base = static_cast<stream<Derived, Raw>&>(self);

      if (base.allocate_callback_) {
        detail::invoke_callback([&] {
          auto out = base.allocate_callback_(self, suggested_size);
          *buf = *out.native();
        });
      } else {
        *buf = uv_buf_init(nullptr, 0);
      }
    }

    static void read_trampoline(uv_stream_t *raw, ssize_t nread, const uv_buf_t *buf) noexcept {
      auto &self = Derived::from_native(reinterpret_cast<Raw *>(raw));
      auto &base = static_cast<stream<Derived, Raw>&>(self);

      if (base.read_callback_) {
        detail::invoke_callback(base.read_callback_, self, read_result{nread, buf});
      }
    }

    static void write_trampoline(uv_write_t *raw, int status) noexcept {
      write_request::from_native(raw).invoke(status);
    }

    static void shutdown_trampoline(uv_shutdown_t *raw, int status) noexcept {
      shutdown_request::from_native(raw).invoke(status);
    }

    allocate_callback allocate_callback_{};
    read_callback read_callback_{};
    connection_callback connection_callback_{};
  };

}
