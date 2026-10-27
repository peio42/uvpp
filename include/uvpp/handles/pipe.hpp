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
#include "uvpp/detail/initializable_handle.hpp"
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

}

namespace uv::raw {

  class pipe final : public detail::initializable_handle<pipe, uv_pipe_t, stream<pipe, uv_pipe_t>> {
    using base = detail::initializable_handle<pipe, uv_pipe_t, stream<pipe, uv_pipe_t>>;
  public:
    pipe() noexcept = default;
    explicit pipe(loop &l, bool ipc = false) : pipe(l.view(), ipc) {}
    explicit pipe(loop_view l, bool ipc = false) { init(l, ipc).throw_if_error(); }

    status init(loop &l, bool ipc = false) noexcept { return init(l.view(), ipc); }
    status init(loop_view l, bool ipc = false) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      // Both native backends initialize without a fallible post-registration
      // step. No TCP/poll rollback or extra native lifetime allocation is needed.
      return status::from_native(uv_pipe_init(l.native(), native(), ipc ? 1 : 0));
    }

    status bind(std::string_view name) {
#if UVPP_HAS_PIPE_BIND2
      return bind(name, 0);
#else
      if (!ready()) return status::from_native(UV_EBADF);
      if (name.empty() || name.find('\0') != std::string_view::npos) return status::from_native(UV_EINVAL);
      std::string storage{name};
      return status::from_native(uv_pipe_bind(native(), storage.c_str()));
#endif
    }

#if UVPP_HAS_PIPE_BIND2
    status bind(std::string_view name, unsigned int flags) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      if (name.empty()) return status::from_native(UV_EINVAL);
      return status::from_native(uv_pipe_bind2(native(), name.data(), name.size(), flags));
    }

    status bind(std::string_view name, pipe_name_flag flag) noexcept {
      return bind(name, static_cast<unsigned int>(flag));
    }
#endif

    result<std::string> sockname() const {
      if (!ready()) return result<std::string>{make_error_code(UV_EBADF)};
      return get_name([this](char *buffer, std::size_t *size) {
        return uv_pipe_getsockname(native(), buffer, size);
      });
    }

    result<std::string> peername() const {
      if (!ready()) return result<std::string>{make_error_code(UV_EBADF)};
      return get_name([this](char *buffer, std::size_t *size) {
        return uv_pipe_getpeername(native(), buffer, size);
      });
    }

    status connect(connect_request &request, std::string_view name, connect_request::callback callback) {
#if UVPP_HAS_PIPE_CONNECT2
      return connect(request, name, 0, std::move(callback));
#else
      return detail::stream_request_submission::submit(request, std::move(callback), [&] {
        if (!ready()) return int{UV_EBADF};
        if (name.empty() || name.find('\0') != std::string_view::npos) return int{UV_EINVAL};
        std::string storage{name};
        uv_pipe_connect(request.native(), native(), storage.c_str(), connect_request::trampoline);
        return 0; // Legacy native connect is void; failure arrives at completion.
      });
#endif
    }

#if UVPP_HAS_PIPE_CONNECT2
    status connect(connect_request &request, std::string_view name, unsigned int flags,
                   connect_request::callback callback) noexcept {
      return detail::stream_request_submission::submit(request, std::move(callback), [&]() noexcept {
        if (!ready()) return int{UV_EBADF};
        if (name.empty()) return int{UV_EINVAL};
        return uv_pipe_connect2(request.native(), native(), name.data(), name.size(), flags, connect_request::trampoline);
      });
    }

    status connect(connect_request &request, std::string_view name, pipe_name_flag flag,
                   connect_request::callback callback) noexcept {
      return connect(request, name, static_cast<unsigned int>(flag), std::move(callback));
    }
#endif

    template<auto Callback>
      requires std::invocable<decltype(Callback), connect_request&, uv::status>
    status connect_static(connect_request &request, std::string_view name) {
#if UVPP_HAS_PIPE_CONNECT2
      return connect_static<Callback>(request, name, 0);
#else
      return detail::stream_request_submission::submit_static<Callback>(request, [&] {
        if (!ready()) return int{UV_EBADF};
        if (name.empty() || name.find('\0') != std::string_view::npos) return int{UV_EINVAL};
        std::string storage{name};
        uv_pipe_connect(request.native(), native(), storage.c_str(), connect_request::trampoline);
        return 0;
      });
#endif
    }

#if UVPP_HAS_PIPE_CONNECT2
    template<auto Callback>
      requires std::invocable<decltype(Callback), connect_request&, uv::status>
    status connect_static(connect_request &request, std::string_view name, unsigned int flags) noexcept {
      return detail::stream_request_submission::submit_static<Callback>(request, [&]() noexcept {
        if (!ready()) return int{UV_EBADF};
        if (name.empty()) return int{UV_EINVAL};
        return uv_pipe_connect2(request.native(), native(), name.data(), name.size(), flags, connect_request::trampoline);
      });
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), connect_request&, uv::status>
    status connect_static(connect_request &request, std::string_view name, pipe_name_flag flag) noexcept {
      return connect_static<Callback>(request, name, static_cast<unsigned int>(flag));
    }
#endif

    status open(uv_file file) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      return status::from_native(uv_pipe_open(native(), file));
    }

    status pending_instances(int count) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      if (count <= 0) return status::from_native(UV_EINVAL);
      uv_pipe_pending_instances(native(), count);
      return {};
    }

    int pending_count() const noexcept {
      return initialized() ? uv_pipe_pending_count(const_cast<uv_pipe_t *>(native())) : 0;
    }

    handle_type pending_type() const noexcept {
      return initialized() ? static_cast<handle_type>(uv_pipe_pending_type(const_cast<uv_pipe_t *>(native())))
                           : handle_type::unknown;
    }

    status chmod(int flags) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      return status::from_native(uv_pipe_chmod(native(), flags));
    }

    status chmod(pipe_chmod_flag flag) noexcept { return chmod(static_cast<int>(flag)); }

    template<stream_handle SendHandle>
    status write_with_handle(write_request &request, std::span<const buffer_view> buffers,
                             SendHandle &send_handle, write_request::callback callback) noexcept {
      return detail::stream_request_submission::submit(request, std::move(callback), [&]() noexcept {
        if (const auto code = check_export(send_handle); code < 0) return code;
        if (!this->valid_buffers(buffers)) return int{UV_EINVAL};
        return uv_write2(request.native(), this->native_stream(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                         detail::narrow_buffer_count_unchecked(buffers.size()), send_handle.native_stream(),
                         write_request::trampoline);
      });
    }

    template<stream_handle SendHandle>
    status write_with_handle(write_request &request, const buffer_view &buffer, SendHandle &send_handle,
                             write_request::callback callback) noexcept {
      return write_with_handle(request, std::span<const buffer_view>{&buffer, 1}, send_handle, std::move(callback));
    }

    template<auto Callback, stream_handle SendHandle>
      requires std::invocable<decltype(Callback), write_request&, uv::status>
    status write_with_handle_static(write_request &request, std::span<const buffer_view> buffers,
                                    SendHandle &send_handle) noexcept {
      return detail::stream_request_submission::submit_static<Callback>(request, [&]() noexcept {
        if (const auto code = check_export(send_handle); code < 0) return code;
        if (!this->valid_buffers(buffers)) return int{UV_EINVAL};
        return uv_write2(request.native(), this->native_stream(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                         detail::narrow_buffer_count_unchecked(buffers.size()), send_handle.native_stream(),
                         write_request::trampoline);
      });
    }

    template<auto Callback, stream_handle SendHandle>
      requires std::invocable<decltype(Callback), write_request&, uv::status>
    status write_with_handle_static(write_request &request, const buffer_view &buffer, SendHandle &send_handle) noexcept {
      return write_with_handle_static<Callback>(request, std::span<const buffer_view>{&buffer, 1}, send_handle);
    }

    template<stream_handle SendHandle>
    write_now_result write_with_handle_now(std::span<const buffer_view> buffers, SendHandle &send_handle) noexcept {
      if (const auto code = check_export(send_handle); code < 0) return write_now_result{code};
      if (!this->valid_buffers(buffers)) return write_now_result{UV_EINVAL};
      return write_now_result{uv_try_write2(this->native_stream(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                               detail::narrow_buffer_count_unchecked(buffers.size()), send_handle.native_stream())};
    }

    template<stream_handle SendHandle>
    write_now_result write_with_handle_now(const buffer_view &buffer, SendHandle &send_handle) noexcept {
      return write_with_handle_now(std::span<const buffer_view>{&buffer, 1}, send_handle);
    }

  private:
    bool ready() const noexcept { return initialized() && !closing(); }

    template<stream_handle SendHandle>
    int check_export(SendHandle &send_handle) const noexcept {
      if (!ready()) return UV_EBADF;
      if (!native()->ipc) return UV_EINVAL;
      auto *target = send_handle.native_stream();
      if (!target->loop || uv_is_closing(reinterpret_cast<uv_handle_t *>(target))) return UV_EBADF;
      if (target->type != UV_TCP && target->type != UV_NAMED_PIPE) return UV_EINVAL;
      return 0;
    }

    template<class Getter>
    static result<std::string> get_name(Getter getter) {
      std::vector<char> buffer(256);
      for (;;) {
        auto size = buffer.size();
        const auto code = getter(buffer.data(), &size);
        if (code == UV_ENOBUFS) { buffer.resize(size); continue; }
        if (code < 0) return result<std::string>{make_error_code(code)};
        // libuv reports payload length, excluding any terminator. Preserve binary
        // Linux abstract names, including a meaningful trailing NUL byte.
        return result<std::string>{std::string{buffer.data(), size}};
      }
    }
  };

}
