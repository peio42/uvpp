#pragma once

#include <concepts>
#include <coroutine>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/buffer.hpp"

namespace uv {

class tcp_listener;

namespace detail {

struct tcp_connection_state {
  uv_connect_t connect{};
  uv_tcp_t tcp{};
  uv::loop *loop = nullptr;
  std::coroutine_handle<> connect_continuation{};
  std::coroutine_handle<> close_continuation{};
  int connect_status = 0;
  bool initialized = false;
  bool close_started = false;
  bool write_active = false;
  void *active_read = nullptr;
  bool destroy_on_close = false;

  static tcp_connection_state &from_connect(uv_connect_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_connection_state *>(bytes - offsetof(tcp_connection_state, connect));
  }

  static tcp_connection_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<tcp_connection_state *>(bytes - offsetof(tcp_connection_state, tcp));
  }

  void close(std::coroutine_handle<> continuation, bool destroy_when_closed) noexcept {
    if (close_started) {
      std::terminate();
    }
    close_started = true;
    close_continuation = continuation;
    destroy_on_close = destroy_when_closed;
    uv_close(reinterpret_cast<uv_handle_t *>(&tcp), &tcp_connection_state::on_close);
  }

  static void on_connect(uv_connect_t *raw, int status) noexcept {
    auto &self = from_connect(raw);
    self.connect_status = status;
    auto continuation = std::exchange(self.connect_continuation, {});
    if (status < 0) {
      self.close(continuation, false);
      return;
    }
    continuation.resume();
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    auto continuation = std::exchange(self.close_continuation, {});
    const bool destroy = self.destroy_on_close;
    self.destroy_on_close = false;
    if (continuation) {
      continuation.resume();
    }
    if (destroy) {
      delete &self;
    }
  }
};

static_assert(std::is_standard_layout_v<tcp_connection_state>);

} // namespace detail

// Experimental v3 movable TCP client owner. Moving transfers the stable state;
// it never relocates the uv_tcp_t. Destruction starts an internal asynchronous
// close and releases the state only from the close callback.
class tcp_connection {
public:
  tcp_connection(const tcp_connection &) = delete;
  tcp_connection &operator=(const tcp_connection &) = delete;

  tcp_connection(tcp_connection &&other) noexcept : state_{std::move(other.state_)} {}

  tcp_connection &operator=(tcp_connection &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  ~tcp_connection() { reset(); }

  uv_tcp_t *native_handle() noexcept { return state_ ? &state_->tcp : nullptr; }
  const uv_tcp_t *native_handle() const noexcept { return state_ ? &state_->tcp : nullptr; }
  bool closing() const noexcept { return state_ && state_->close_started; }

  class read_some_result {
  public:
    read_some_result(std::size_t count, bool eof) noexcept : count_{count}, eof_{eof} {}

    std::size_t count() const noexcept { return count_; }
    bool eof() const noexcept { return eof_; }

  private:
    std::size_t count_ = 0;
    bool eof_ = false;
  };

  class read_awaiter {
  public:
    read_awaiter(detail::tcp_connection_state *state, std::span<std::byte> buffer) noexcept
      : state_{state}, buffer_{buffer} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->close_started) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != state_->loop) {
        throw std::logic_error{"uv::tcp_connection read used from a different loop"};
      }
      if (state_->active_read != nullptr) {
        status_ = UV_EBUSY;
        return false;
      }
      if (buffer_.empty()) {
        status_ = UV_EINVAL;
        return false;
      }
      state_->active_read = this;
      continuation_ = continuation;
      status_ = uv_read_start(reinterpret_cast<uv_stream_t *>(&state_->tcp),
          &read_awaiter::on_alloc, &read_awaiter::on_read);
      if (status_ < 0) {
        state_->active_read = nullptr;
        continuation_ = {};
        return false;
      }
      return true;
    }

    read_some_result await_resume() {
      if (status_ == UV_EOF) {
        return {0, true};
      }
      if (status_ < 0) {
        throw_if_error(static_cast<int>(status_));
      }
      return {static_cast<std::size_t>(status_), false};
    }

  private:
    static read_awaiter &active(uv_handle_t *raw) noexcept {
      auto &state = detail::tcp_connection_state::from_handle(raw);
      return *static_cast<read_awaiter *>(state.active_read);
    }

    static void on_alloc(uv_handle_t *raw, size_t, uv_buf_t *out) noexcept {
      auto &self = active(raw);
      *out = detail::make_native_buffer_unchecked(
          reinterpret_cast<char *>(self.buffer_.data()), self.buffer_.size());
    }

    static void on_read(uv_stream_t *raw, ssize_t nread, const uv_buf_t *) noexcept {
      auto &self = active(reinterpret_cast<uv_handle_t *>(raw));
      if (nread == 0) {
        return;
      }
      self.status_ = nread;
      // libuv guarantees that uv_read_stop() prevents future read callbacks.
      // Its non-zero TTY/Windows return does not indicate a stop failure, and
      // tcp_connection is a TCP-only owner in any case.
      (void)uv_read_stop(raw);
      auto &state = *self.state_;
      state.active_read = nullptr; // Release alloc/read slots before user resumption.
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }

    detail::tcp_connection_state *state_ = nullptr;
    std::span<std::byte> buffer_{};
    std::coroutine_handle<> continuation_{};
    ssize_t status_ = 0;
  };

  // Borrows buffer storage until data, EOF, or an error has stopped the read.
  [[nodiscard]] read_awaiter read_some(std::span<std::byte> buffer) noexcept {
    return read_awaiter{state_.get(), buffer};
  }

  class write_awaiter {
  public:
    write_awaiter(detail::tcp_connection_state *state, std::string_view data) noexcept
      : state_{state}, buffer_{detail::make_native_buffer_unchecked(
          const_cast<char *>(data.data()), data.size())} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->close_started) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != state_->loop) {
        throw std::logic_error{"uv::tcp_connection write used from a different loop"};
      }
      if (state_->write_active) {
        status_ = UV_EBUSY;
        return false;
      }
      state_->write_active = true;
      continuation_ = continuation;
      status_ = uv_write(&request_, reinterpret_cast<uv_stream_t *>(&state_->tcp),
          &buffer_, 1, &write_awaiter::on_write);
      if (status_ < 0) {
        state_->write_active = false;
        continuation_ = {};
        return false;
      }
      return true;
    }

    void await_resume() { throw_if_error(status_); }

  private:
    static write_awaiter &from_native(uv_write_t *raw) noexcept {
      return *reinterpret_cast<write_awaiter *>(raw);
    }

    static void on_write(uv_write_t *raw, int status) noexcept {
      auto &self = from_native(raw);
      self.status_ = status;
      self.state_->write_active = false;
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }

    uv_write_t request_{};
    detail::tcp_connection_state *state_ = nullptr;
    uv_buf_t buffer_{};
    std::coroutine_handle<> continuation_{};
    int status_ = 0;
  };

  // Borrows data until the native write completion invokes the awaiting task.
  // The caller must keep it alive, address-stable, and unmodified until then.
  [[nodiscard]] write_awaiter write(std::string_view data) {
    detail::check_buffer_length(data.size());
    return write_awaiter{state_.get(), data};
  }

  class connect_awaiter {
  public:
    explicit connect_awaiter(const ipv4 &address) noexcept {
      std::memcpy(&peer_, address.native(), sizeof(sockaddr_in));
    }

    explicit connect_awaiter(const ipv6 &address) noexcept {
      std::memcpy(&peer_, address.native(), sizeof(sockaddr_in6));
    }

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      state_ = std::make_unique<detail::tcp_connection_state>();
      auto &state = *state_;
      state.loop = &continuation.promise().execution_loop();
      state.connect_continuation = continuation;
      state.connect_status = uv_tcp_init(state.loop->native(), &state.tcp);
      if (state.connect_status < 0) {
        return false;
      }
      state.initialized = true;
      state.connect_status = uv_tcp_connect(&state.connect, &state.tcp,
          reinterpret_cast<const sockaddr *>(&peer_), &detail::tcp_connection_state::on_connect);
      if (state.connect_status < 0) {
        auto resume = std::exchange(state.connect_continuation, {});
        state.close(resume, false);
      }
      return true;
    }

    tcp_connection await_resume() {
      throw_if_error(state_->connect_status);
      return tcp_connection{std::move(state_)};
    }

  private:
    sockaddr_storage peer_{};
    std::unique_ptr<detail::tcp_connection_state> state_{};
  };

  [[nodiscard]] static connect_awaiter connect(const ipv4 &address) noexcept {
    return connect_awaiter{address};
  }

  [[nodiscard]] static connect_awaiter connect(const ipv6 &address) noexcept {
    return connect_awaiter{address};
  }

private:
  explicit tcp_connection(std::unique_ptr<detail::tcp_connection_state> state) noexcept
    : state_{std::move(state)} {}

  void reset() noexcept {
    if (!state_) {
      return;
    }
    auto *state = state_.release();
    assert(!state->write_active);
    assert(state->active_read == nullptr);
    if (state->write_active || state->active_read != nullptr) {
      std::terminate();
    }
    state->close({}, true);
  }

  std::unique_ptr<detail::tcp_connection_state> state_{};

  friend class tcp_listener;
};

static_assert(std::is_standard_layout_v<tcp_connection::write_awaiter>);

} // namespace uv
