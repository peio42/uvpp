#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/stream.hpp"
#include "uvpp/requests/connect.hpp"

namespace uv {

  enum class pipe_chmod_flag : int {
    readable = UV_READABLE,
    writable = UV_WRITABLE
  };

  constexpr int operator|(pipe_chmod_flag lhs, pipe_chmod_flag rhs) noexcept {
    return static_cast<int>(lhs) | static_cast<int>(rhs);
  }

  constexpr int operator|(int lhs, pipe_chmod_flag rhs) noexcept {
    return lhs | static_cast<int>(rhs);
  }

  class pipe final : public stream<pipe, uv_pipe_t> {
  public:
    explicit pipe(loop &l, bool ipc = false) {
      throw_if_error(uv_pipe_init(l.native(), native(), ipc ? 1 : 0));
    }

    explicit pipe(loop_view l, bool ipc = false) {
      throw_if_error(uv_pipe_init(l.native(), native(), ipc ? 1 : 0));
    }

    void bind(std::string_view name) {
      std::string storage{name};
      throw_if_error(uv_pipe_bind(native(), storage.c_str()));
    }

    std::string sockname() const {
      return get_name([this](char *buffer, std::size_t *size) {
        return uv_pipe_getsockname(native(), buffer, size);
      });
    }

    std::string peername() const {
      return get_name([this](char *buffer, std::size_t *size) {
        return uv_pipe_getpeername(native(), buffer, size);
      });
    }

    void connect(connect_request &request, std::string_view name, connect_request::callback callback) {
      request.set_callback(std::move(callback));
      std::string storage{name};
      uv_pipe_connect(request.native(), native(), storage.c_str(), &pipe::connect_trampoline);
    }

    template<auto Callback>
    void connect_static(connect_request &request, std::string_view name) {
      std::string storage{name};
      uv_pipe_connect(request.native(), native(), storage.c_str(), [](uv_connect_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(connect_request::from_native(raw), result{status});
      });
    }

    void open(uv_file file) {
      throw_if_error(uv_pipe_open(native(), file));
    }

    void pending_instances(int count) noexcept {
      uv_pipe_pending_instances(native(), count);
    }

    int pending_count() const noexcept {
      return uv_pipe_pending_count(const_cast<uv_pipe_t *>(native()));
    }

    uv_handle_type pending_type() const noexcept {
      return uv_pipe_pending_type(const_cast<uv_pipe_t *>(native()));
    }

    void chmod(int flags) {
      throw_if_error(uv_pipe_chmod(native(), flags));
    }

    void chmod(pipe_chmod_flag flag) {
      chmod(static_cast<int>(flag));
    }

    template<class SendHandle>
      requires requires(SendHandle &h) { h.native_stream(); }
    void write_with_handle(write_request &request, std::span<const buffer_view> buffers,
                           SendHandle &send_handle, write_request::callback callback) {
      detail::submit_request(request, std::move(callback), [&] {
        return uv_write2(request.native(), native_stream(),
                         reinterpret_cast<const uv_buf_t *>(buffers.data()),
                         static_cast<unsigned int>(buffers.size()),
                         send_handle.native_stream(),
                         write_request::trampoline);
      });
    }

    template<class SendHandle>
      requires requires(SendHandle &h) { h.native_stream(); }
    write_now_result write_with_handle_now(std::span<const buffer_view> buffers,
                                           SendHandle &send_handle) noexcept {
      return write_now_result{uv_try_write2(native_stream(),
        reinterpret_cast<const uv_buf_t *>(buffers.data()),
        static_cast<unsigned int>(buffers.size()),
        send_handle.native_stream())};
    }

  private:
    template<class Getter>
    static std::string get_name(Getter getter) {
      std::vector<char> buffer(256);

      for (;;) {
        auto size = buffer.size();
        auto status = getter(buffer.data(), &size);

        if (status == UV_ENOBUFS) {
          buffer.resize(size);
          continue;
        }

        throw_if_error(status);
        if (size > 0 && buffer[size - 1] == '\0') {
          --size;
        }

        return std::string{buffer.data(), size};
      }
    }

    static void connect_trampoline(uv_connect_t *raw, int status) noexcept {
      connect_request::from_native(raw).invoke(status);
    }
  };

}
