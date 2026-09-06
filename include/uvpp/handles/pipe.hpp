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
#include "uvpp/core/version.hpp"
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

#if UVPP_HAS_PIPE_BIND2 || UVPP_HAS_PIPE_CONNECT2
  enum class pipe_name_flag : unsigned int {
    no_truncate = UV_PIPE_NO_TRUNCATE
  };

  constexpr unsigned int operator|(pipe_name_flag lhs, pipe_name_flag rhs) noexcept {
    return static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs);
  }

  constexpr unsigned int operator|(unsigned int lhs, pipe_name_flag rhs) noexcept {
    return lhs | static_cast<unsigned int>(rhs);
  }
#endif

  class pipe final : public stream<pipe, uv_pipe_t> {
  public:
    explicit pipe(loop &l, bool ipc = false) {
      throw_if_error(uv_pipe_init(l.native(), native(), ipc ? 1 : 0));
    }

    explicit pipe(loop_view l, bool ipc = false) {
      throw_if_error(uv_pipe_init(l.native(), native(), ipc ? 1 : 0));
    }

    void bind(std::string_view name) {
#if UVPP_HAS_PIPE_BIND2
      throw_if_error(uv_pipe_bind2(native(), name.data(), name.size(), 0));
#else
      std::string storage{name};
      throw_if_error(uv_pipe_bind(native(), storage.c_str()));
#endif
    }

#if UVPP_HAS_PIPE_BIND2
    void bind(std::string_view name, unsigned int flags) {
      throw_if_error(uv_pipe_bind2(native(), name.data(), name.size(), flags));
    }

    void bind(std::string_view name, pipe_name_flag flag) {
      bind(name, static_cast<unsigned int>(flag));
    }
#endif

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
#if UVPP_HAS_PIPE_CONNECT2
      detail::submit_request(request, std::move(callback), [&] {
        return uv_pipe_connect2(request.native(), native(), name.data(), name.size(), 0, &pipe::connect_trampoline);
      });
#else
      request.set_callback(std::move(callback));
      std::string storage{name};
      uv_pipe_connect(request.native(), native(), storage.c_str(), &pipe::connect_trampoline);
#endif
    }

#if UVPP_HAS_PIPE_CONNECT2
    void connect(connect_request &request, std::string_view name, unsigned int flags,
                 connect_request::callback callback) {
      detail::submit_request(request, std::move(callback), [&] {
        return uv_pipe_connect2(request.native(), native(), name.data(), name.size(),
                                flags, &pipe::connect_trampoline);
      });
    }

    void connect(connect_request &request, std::string_view name, pipe_name_flag flag,
                 connect_request::callback callback) {
      connect(request, name, static_cast<unsigned int>(flag), std::move(callback));
    }
#endif

    template<auto Callback>
    void connect_static(connect_request &request, std::string_view name) {
#if UVPP_HAS_PIPE_CONNECT2
      connect_static<Callback>(request, name, 0);
#else
      std::string storage{name};
      uv_pipe_connect(request.native(), native(), storage.c_str(), [](uv_connect_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(connect_request::from_native(raw), result{status});
      });
#endif
    }

#if UVPP_HAS_PIPE_CONNECT2
    template<auto Callback>
    void connect_static(connect_request &request, std::string_view name, unsigned int flags) {
      throw_if_error(uv_pipe_connect2(request.native(), native(), name.data(), name.size(), flags,
        [](uv_connect_t *raw, int status) noexcept {
          detail::invoke_static_callback<Callback>(connect_request::from_native(raw), result{status});
        }));
    }

    template<auto Callback>
    void connect_static(connect_request &request, std::string_view name, pipe_name_flag flag) {
      connect_static<Callback>(request, name, static_cast<unsigned int>(flag));
    }
#endif

    void open(uv_file file) {
      throw_if_error(uv_pipe_open(native(), file));
    }

    void pending_instances(int count) noexcept {
      uv_pipe_pending_instances(native(), count);
    }

    int pending_count() const noexcept {
      return uv_pipe_pending_count(const_cast<uv_pipe_t *>(native()));
    }

    handle_type pending_type() const noexcept {
      return static_cast<handle_type>(uv_pipe_pending_type(const_cast<uv_pipe_t *>(native())));
    }

    void chmod(int flags) {
      throw_if_error(uv_pipe_chmod(native(), flags));
    }

    void chmod(pipe_chmod_flag flag) {
      chmod(static_cast<int>(flag));
    }

    template<stream_handle SendHandle>
    void write_with_handle(write_request &request, std::span<const buffer_view> buffers,
                           SendHandle &send_handle, write_request::callback callback) {
      detail::submit_request(request, std::move(callback), [&] {
        return uv_write2(request.native(), native_stream(),
                         reinterpret_cast<const uv_buf_t *>(buffers.data()),
                         detail::checked_buffer_count(buffers.size()),
                         send_handle.native_stream(),
                         write_request::trampoline);
      });
    }

    template<stream_handle SendHandle>
    write_now_result write_with_handle_now(std::span<const buffer_view> buffers,
                                           SendHandle &send_handle) noexcept {
      if (!detail::buffer_count_fits(buffers.size())) {
        return write_now_result{UV_EINVAL};
      }
      return write_now_result{uv_try_write2(native_stream(),
        reinterpret_cast<const uv_buf_t *>(buffers.data()),
        detail::narrow_buffer_count_unchecked(buffers.size()),
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
