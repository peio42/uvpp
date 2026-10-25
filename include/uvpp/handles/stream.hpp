#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <span>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/handles/handle.hpp"
#include "uvpp/core/buffer.hpp"
#include "uvpp/requests/shutdown.hpp"
#include "uvpp/requests/write.hpp"

namespace uv {

  class read_result {
  public:
    read_result(ssize_t nread, const uv_buf_t *buf) noexcept
      : nread_{nread}, buffer_{buf ? buffer_view::from_native(*buf) : buffer_view{}} {}

    bool ok() const noexcept { return nread_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    bool eof() const noexcept { return nread_ == UV_EOF; }
    ssize_t count() const noexcept { return nread_; }
    int raw_status() const noexcept { return nread_ < 0 ? static_cast<int>(nread_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }
    uv::status status() const noexcept { return uv::status::from_native(raw_status()); }

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

  class write_now_result {
  public:
    explicit write_now_result(int value) noexcept : value_{value} {}

    bool ok()          const noexcept { return value_ >= 0; }
    bool would_block() const noexcept { return value_ == UV_EAGAIN; }
    bool has_error()   const noexcept { return value_ < 0 && value_ != UV_EAGAIN; }

    std::size_t bytes_written() const noexcept {
      return ok() ? static_cast<std::size_t>(value_) : 0;
    }

    int raw() const noexcept { return value_; }

    uv::error_code error_code() const noexcept {
      return has_error() ? make_error_code(value_) : uv::error_code{};
    }

  private:
    int value_;
  };

  template<class T>
  concept stream_handle = requires(T &handle) {
    { handle.native_stream() } -> std::convertible_to<uv_stream_t *>;
  };

}

namespace uv::raw {

  template<class Derived, class Raw>
  class stream : public basic_handle<Derived, Raw> {
    using base = basic_handle<Derived, Raw>;
  public:
    using typename base::close_callback;
    using allocate_callback = std::function<buffer_view(Derived&, std::size_t)>;
    using read_callback = std::function<void(Derived&, read_result)>;
    using connection_callback = std::function<void(Derived&, uv::status)>;

    uv_stream_t *native_stream() noexcept {
      return reinterpret_cast<uv_stream_t *>(this->native());
    }

    const uv_stream_t *native_stream() const noexcept {
      return reinterpret_cast<const uv_stream_t *>(this->native());
    }

    std::size_t write_queue_size() const noexcept {
      return (this->native_handle()->loop != nullptr) ? uv_stream_get_write_queue_size(native_stream()) : 0;
    }

    bool readable() const noexcept {
      return (this->native_handle()->loop != nullptr) && uv_is_readable(native_stream()) != 0;
    }

    bool writable() const noexcept {
      return (this->native_handle()->loop != nullptr) && uv_is_writable(native_stream()) != 0;
    }

    status blocking(bool enable) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      return status::from_native(uv_stream_set_blocking(native_stream(), enable ? 1 : 0));
    }

    template<stream_handle Client>
    status accept(Client &client) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      auto *target = client.native_stream();
      if (!target->loop || uv_is_closing(reinterpret_cast<uv_handle_t *>(target)))
        return status::from_native(UV_EBADF);
      if (target->loop != native_stream()->loop ||
          (target->type != UV_TCP && target->type != UV_NAMED_PIPE) ||
          (native_stream()->type != UV_TCP && native_stream()->type != UV_NAMED_PIPE) ||
          (native_stream()->type == UV_TCP && target->type != UV_TCP))
        return status::from_native(UV_EINVAL);
      if (target == native_stream()) return status::from_native(UV_EINVAL);
      if (native_stream()->type == UV_NAMED_PIPE) {
        auto *pipe = reinterpret_cast<uv_pipe_t *>(native_stream());
        if (pipe->ipc) {
          if (uv_pipe_pending_count(pipe) == 0) return status::from_native(UV_EAGAIN);
          if (uv_pipe_pending_type(pipe) != target->type) return status::from_native(UV_EINVAL);
        } else if (target->type != UV_NAMED_PIPE) return status::from_native(UV_EINVAL);
        else if (!listening_) return status::from_native(UV_EINVAL);
      } else if (!listening_) return status::from_native(UV_EINVAL);
      return status::from_native(uv_accept(native_stream(), target));
    }

    status listen(int backlog, connection_callback callback) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      if (!can_listen() || !callback || backlog <= 0) return status::from_native(UV_EINVAL);
      auto result = status::from_native(uv_listen(native_stream(), backlog, &stream::connection_trampoline));
      if (result) {
        listening_ = true;
        connection_callback_.replace(std::move(callback));
        static_connection_ = nullptr;
      } else if (!this->active()) {
        listening_ = false;
        connection_callback_.replace({});
        static_connection_ = nullptr;
      }
      return result;
    }

    status listen(connection_callback callback) noexcept {
      return listen(64, std::move(callback));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), Derived&, uv::status>
    status listen_static(int backlog = 64) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      if (!can_listen() || backlog <= 0) return status::from_native(UV_EINVAL);
      auto result = status::from_native(uv_listen(native_stream(), backlog, &stream::connection_trampoline));
      if (result) {
        listening_ = true;
        connection_callback_.replace({});
        static_connection_ = [](Derived &self, uv::status result) noexcept {
          detail::invoke_static_callback<Callback>(self, result);
        };
      } else if (!this->active()) {
        listening_ = false;
        connection_callback_.replace({});
        static_connection_ = nullptr;
      }
      return result;
    }

    status read_start(allocate_callback allocator, read_callback reader) noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      if (!allocator || !reader) return status::from_native(UV_EINVAL);
      read_subscription next{std::move(allocator), std::move(reader)};
      return start_read(std::move(next));
    }

    template<auto Allocator, auto Reader>
      requires std::invocable<decltype(Allocator), Derived&, std::size_t> &&
               std::convertible_to<std::invoke_result_t<decltype(Allocator), Derived&, std::size_t>, buffer_view> &&
               std::invocable<decltype(Reader), Derived&, read_result>
    status read_start_static() noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      read_subscription next{};
      next.static_allocate = [](Derived &self, std::size_t size) { return Allocator(self, size); };
      next.static_read = [](Derived &self, read_result result) { Reader(self, result); };
      return start_read(std::move(next));
    }

    status read_stop() noexcept {
      if (!ready()) return status::from_native(UV_EBADF);
      auto result = status::from_native(uv_read_stop(native_stream()));
      if (result) {
        reading_ = false;
        read_subscription_.replace({});
      }
      return result;
    }

    void close() noexcept {
      if (!ready()) return;
      uv_close(this->native_handle(), &stream::close_trampoline);
      release_callbacks();
    }

    void close(close_callback callback) noexcept {
      if (!ready()) return;
      this->set_close_callback(std::move(callback));
      uv_close(this->native_handle(), &stream::close_trampoline);
      release_callbacks();
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), Derived&>
    void close_static() noexcept {
      if (!ready()) return;
      uv_close(this->native_handle(), [](uv_handle_t *raw) noexcept {
        auto &self = Derived::from_native(raw);
        self.finish_close();
        detail::invoke_static_callback<Callback>(self);
      });
      release_callbacks();
    }

    status shutdown(shutdown_request &request, shutdown_request::callback callback) noexcept {
      return detail::stream_request_submission::submit(request, std::move(callback), [&]() noexcept {
        if (!ready()) return int{UV_EBADF};
        return uv_shutdown(request.native(), native_stream(), shutdown_request::trampoline);
      });
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), shutdown_request&, uv::status>
    status shutdown_static(shutdown_request &request) noexcept {
      return detail::stream_request_submission::submit_static<Callback>(request, [&]() noexcept {
        if (!ready()) return int{UV_EBADF};
        return uv_shutdown(request.native(), native_stream(), shutdown_request::trampoline);
      });
    }

    status write(write_request &request, std::span<const buffer_view> buffers, write_request::callback callback) noexcept {
      return detail::stream_request_submission::submit(request, std::move(callback), [&]() noexcept {
        if (!ready()) return int{UV_EBADF};
        if (!valid_buffers(buffers)) return int{UV_EINVAL};
        return uv_write(request.native(), native_stream(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                        detail::narrow_buffer_count_unchecked(buffers.size()), write_request::trampoline);
      });
    }

    status write(write_request &request, const buffer_view &buf, write_request::callback callback) noexcept {
      return write(request, std::span<const buffer_view>{&buf, 1}, std::move(callback));
    }

    status write(write_request &request, std::span<const std::byte> bytes, write_request::callback callback) noexcept {
      return detail::stream_request_submission::submit(request, std::move(callback), [&]() noexcept {
        if (!ready()) return int{UV_EBADF};
        if (!detail::buffer_length_fits(bytes.size())) return int{UV_EINVAL};
        auto buf = detail::make_native_buffer_unchecked(const_cast<char *>(reinterpret_cast<const char *>(bytes.data())), bytes.size());
        return uv_write(request.native(), native_stream(), &buf, 1, write_request::trampoline);
      });
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), write_request&, uv::status>
    status write_static(write_request &request, std::span<const buffer_view> buffers) noexcept {
      return detail::stream_request_submission::submit_static<Callback>(request, [&]() noexcept {
        if (!ready()) return int{UV_EBADF};
        if (!valid_buffers(buffers)) return int{UV_EINVAL};
        return uv_write(request.native(), native_stream(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                        detail::narrow_buffer_count_unchecked(buffers.size()), write_request::trampoline);
      });
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), write_request&, uv::status>
    status write_static(write_request &request, const buffer_view &buf) noexcept {
      return write_static<Callback>(request, std::span<const buffer_view>{&buf, 1});
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), write_request&, uv::status>
    status write_static(write_request &request, std::span<const std::byte> bytes) noexcept {
      return detail::stream_request_submission::submit_static<Callback>(request, [&]() noexcept {
        if (!ready()) return int{UV_EBADF};
        if (!detail::buffer_length_fits(bytes.size())) return int{UV_EINVAL};
        auto buf = detail::make_native_buffer_unchecked(const_cast<char *>(reinterpret_cast<const char *>(bytes.data())), bytes.size());
        return uv_write(request.native(), native_stream(), &buf, 1, write_request::trampoline);
      });
    }

    write_now_result write_now(std::span<const buffer_view> buffers) noexcept {
      if (!ready()) return write_now_result{UV_EBADF};
      if (!valid_buffers(buffers)) {
        return write_now_result{UV_EINVAL};
      }
      return write_now_result{uv_try_write(native_stream(),
        reinterpret_cast<const uv_buf_t *>(buffers.data()),
        detail::narrow_buffer_count_unchecked(buffers.size()))};
    }

    write_now_result write_now(const buffer_view &buf) noexcept {
      return write_now(std::span<const buffer_view>{&buf, 1});
    }

    write_now_result write_now(std::span<const std::byte> bytes) noexcept {
      if (!ready()) return write_now_result{UV_EBADF};
      if (!detail::buffer_length_fits(bytes.size())) {
        return write_now_result{UV_EINVAL};
      }
      auto raw = detail::make_native_buffer_unchecked(const_cast<char *>(reinterpret_cast<const char *>(bytes.data())),
                                                      bytes.size());
      return write_now_result{uv_try_write(native_stream(), &raw, 1)};
    }

  protected:
    // Stop native delivery before retiring the active subscription. An existing
    // buffer loan remains alive until its read callback or native close completion.
    void release_callbacks() noexcept {
      reading_ = false;
      listening_ = false;
      read_subscription_.replace({});
      connection_callback_.replace({});
      static_connection_ = nullptr;
    }

    void finish_close() noexcept { pending_read_ = {}; }

    static void close_trampoline(uv_handle_t *raw) noexcept {
      auto &self = Derived::from_native(raw);
      self.finish_close();
      basic_handle<Derived, Raw>::close_trampoline(raw);
    }

  private:
    struct read_subscription {
      allocate_callback allocate{};
      read_callback read{};
      buffer_view (*static_allocate)(Derived&, std::size_t) = nullptr;
      void (*static_read)(Derived&, read_result) = nullptr;
      explicit operator bool() const noexcept { return (allocate && read) || (static_allocate && static_read); }
    };
    using read_slot = detail::persistent_callback_slot<read_subscription>;

    bool ready() const noexcept { return (this->native_handle()->loop != nullptr) && !this->closing(); }

    bool can_listen() const noexcept {
      return native_stream()->type == UV_TCP || native_stream()->type == UV_NAMED_PIPE;
    }

    status start_read(read_subscription next) noexcept {
      if (listening_) return status::from_native(UV_EINVAL);
      if (reading_) {
        auto stopped = read_stop();
        if (!stopped) return stopped;
      }
      auto result = status::from_native(uv_read_start(native_stream(), &stream::alloc_trampoline, &stream::read_trampoline));
      if (result) {
        read_subscription_.replace(std::move(next));
        reading_ = true;
      }
      return result;
    }

    static void connection_trampoline(uv_stream_t *raw, int code) noexcept {
      auto &self = Derived::from_native(reinterpret_cast<Raw *>(raw));
      auto &base = static_cast<stream&>(self);
      // Windows can stop native listening before reporting a fatal accept error;
      // Unix may report a per-connection error while listening stays active.
      if (code < 0 && !base.active()) {
        base.listening_ = false;
        auto callback = base.connection_callback_.take();
        auto static_callback = std::exchange(base.static_connection_, nullptr);
        if (static_callback) static_callback(self, uv::status::from_native(code));
        else if (callback) detail::invoke_callback(callback, self, uv::status::from_native(code));
        return;
      }
      if (base.static_connection_) base.static_connection_(self, uv::status::from_native(code));
      else base.connection_callback_.invoke([&](connection_callback &callback) {
        callback(self, uv::status::from_native(code));
      });
    }

    static void alloc_trampoline(uv_handle_t *raw, size_t suggested_size, uv_buf_t *buf) noexcept {
      auto &self = Derived::from_native(raw);
      auto &base = static_cast<stream&>(self);
      // libuv has finished with any previous loan before asking for another.
      base.read_subscription_.restore(std::move(base.pending_read_));
      base.pending_read_ = {};
      auto pending = base.read_subscription_.checkout();
      *buf = detail::make_native_buffer_unchecked(nullptr, 0);
      if (pending.callback) detail::invoke_callback([&] {
        auto &pair = pending.callback;
        auto out = pair.static_allocate ? pair.static_allocate(self, suggested_size) : pair.allocate(self, suggested_size);
        *buf = *out.native();
      });
      base.pending_read_ = std::move(pending);
    }

    static void read_trampoline(uv_stream_t *raw, ssize_t nread, const uv_buf_t *buf) noexcept {
      auto &self = Derived::from_native(reinterpret_cast<Raw *>(raw));
      auto &base = static_cast<stream&>(self);
      auto pending = std::move(base.pending_read_);
      base.pending_read_ = {};
      // Windows zero-byte read errors may arrive without an allocation callback.
      if (!pending.callback) pending = base.read_subscription_.checkout();
      if (nread < 0) {
        if (uv_read_stop(raw) < 0) detail::callback_exception_boundary();
        base.reading_ = false;
        base.read_subscription_.replace({});
      }
      if (pending.callback) detail::invoke_callback([&] {
        auto &pair = pending.callback;
        if (pair.static_read) pair.static_read(self, read_result{nread, buf});
        else pair.read(self, read_result{nread, buf});
      });
      base.read_subscription_.restore(std::move(pending));
    }

    static bool valid_buffers(std::span<const buffer_view> buffers) noexcept {
      if (buffers.empty() || !detail::buffer_count_fits(buffers.size())) return false;
      for (const auto &buffer : buffers)
        if (buffer.size() != 0 && buffer.data() == nullptr) return false;
      return true;
    }

    read_slot read_subscription_{};
    typename read_slot::delivery pending_read_{};
    bool reading_ = false;
    bool listening_ = false;
    void (*static_connection_)(Derived&, uv::status) noexcept = nullptr;
    detail::persistent_callback_slot<connection_callback> connection_callback_{};
  };

}
