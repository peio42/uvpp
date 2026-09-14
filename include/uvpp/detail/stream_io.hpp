#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/net/buffer.hpp"

namespace uv::detail {

// Common stream-operation slots. A stream family keeps this state alongside
// its address-stable native handle; it is not stored in uv_handle_t::data.
struct stream_io_state {
  bool write_active = false;
  void *active_read = nullptr;
};

// An additional object whose native lifetime is borrowed by a stream write.
// IPC handle passing uses this to retain its source handle through uv_write2()
// completion without teaching the common stream state about TCP ownership.
struct stream_write_pin {
  void *context = nullptr;
  uv::loop *loop = nullptr;
  int (*acquire)(void *) noexcept = nullptr;
  void (*release)(void *) noexcept = nullptr;

  bool present() const noexcept { return context != nullptr; }
};

class stream_read_some_result {
public:
  stream_read_some_result(std::size_t count, bool eof) noexcept : count_{count}, eof_{eof} {}

  std::size_t count() const noexcept { return count_; }
  bool eof() const noexcept { return eof_; }

private:
  std::size_t count_ = 0;
  bool eof_ = false;
};

// State is a family-specific, address-stable native state. Besides the stream,
// loop, and I/O-slot accessors, callbacks need its explicit native-handle
// recovery helper; using uv_handle_t::data for this would consume application
// owned libuv storage.
template<class State>
class stream_read_awaiter {
public:
  stream_read_awaiter(State *state, std::span<std::byte> buffer) noexcept
    : state_{state}, buffer_{buffer} {}

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (state_ == nullptr || state_->closing()) {
      status_ = UV_EBADF;
      return false;
    }
    if (&continuation.promise().execution_loop() != &state_->execution_loop()) {
      throw std::logic_error{"uv stream read used from a different loop"};
    }
    if (continuation.promise().stop_requested()) {
      status_ = UV_ECANCELED;
      return false;
    }
    auto &io = state_->io_state();
    if (io.active_read != nullptr) {
      status_ = UV_EBUSY;
      return false;
    }
    if (buffer_.empty()) {
      status_ = UV_EINVAL;
      return false;
    }

    io.active_read = this;
    continuation_ = continuation;
    status_ = uv_read_start(state_->stream_handle(), &stream_read_awaiter::on_alloc,
        &stream_read_awaiter::on_read);
    if (status_ < 0) {
      io.active_read = nullptr;
      continuation_ = {};
      return false;
    }
    cancellation_ = continuation.promise().cancellation();
    if (cancellation_ != nullptr && !cancellation_->register_callback(
        cancellation_registration_, &stream_read_awaiter::on_stop_requested, this)) {
      stop_native_or_terminate();
      release_slots();
      continuation_ = {};
      status_ = UV_ECANCELED;
      return false;
    }
    return true;
  }

  stream_read_some_result await_resume() {
    if (status_ == UV_EOF) {
      return {0, true};
    }
    if (status_ < 0) {
      throw_if_error(static_cast<int>(status_));
    }
    return {static_cast<std::size_t>(status_), false};
  }

private:
  static stream_read_awaiter &active(uv_handle_t *raw) noexcept {
    auto &state = State::from_handle(raw);
    assert(state.io_state().active_read != nullptr);
    return *static_cast<stream_read_awaiter *>(state.io_state().active_read);
  }

  static void on_alloc(uv_handle_t *raw, size_t, uv_buf_t *out) noexcept {
    auto &self = active(raw);
    *out = make_native_buffer_unchecked(
        reinterpret_cast<char *>(self.buffer_.data()), self.buffer_.size());
  }

  static void on_read(uv_stream_t *raw, ssize_t nread, const uv_buf_t *) noexcept {
    auto &self = active(reinterpret_cast<uv_handle_t *>(raw));
    if (nread == 0) {
      return;
    }
    self.status_ = nread;
    self.stop_native_or_terminate();
    self.release_slots(); // Quiesce and release callback slots before user delivery.
    auto continuation = std::exchange(self.continuation_, {});
    continuation.resume();
  }

  static void on_stop_requested(void *context) noexcept {
    auto &self = *static_cast<stream_read_awaiter *>(context);
    if (self.state_ == nullptr || self.state_->io_state().active_read != &self) {
      return;
    }
    self.status_ = UV_ECANCELED;
    self.stop_native_or_terminate();
    self.release_slots(); // cancellation_state removes its registration first
    auto continuation = std::exchange(self.continuation_, {});
    continuation.resume();
  }

  void stop_native_or_terminate() noexcept {
    const int status = uv_read_stop(state_->stream_handle());
    // A terminal callback must leave the source quiescent before its borrowed
    // allocation/read slots may be released. Do not turn an unexpected native
    // stop failure into a post-resume callback that can touch a dead frame.
    assert(status >= 0);
    if (status < 0) {
      std::terminate();
    }
  }

  void release_slots() noexcept {
    if (state_ != nullptr && state_->io_state().active_read == this) {
      state_->io_state().active_read = nullptr;
    }
    if (cancellation_ != nullptr) {
      cancellation_->unregister(cancellation_registration_);
      cancellation_ = nullptr;
    }
  }

  State *state_ = nullptr;
  std::span<std::byte> buffer_{};
  std::coroutine_handle<> continuation_{};
  co::detail::cancellation_state *cancellation_ = nullptr;
  co::detail::cancellation_registration cancellation_registration_{};
  ssize_t status_ = 0;
};

template<class State>
class stream_write_awaiter {
public:
  stream_write_awaiter(State *state, std::string_view data,
      uv_stream_t *send_handle = nullptr, stream_write_pin pin = {},
      int handle_submission_status = 0) noexcept
    : state_{state}, buffer_{make_native_buffer_unchecked(
          const_cast<char *>(data.data()), data.size())}, send_handle_{send_handle},
      pin_{pin}, handle_submission_status_{handle_submission_status} {}

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (state_ == nullptr || state_->closing()) {
      status_ = UV_EBADF;
      return false;
    }
    if (&continuation.promise().execution_loop() != &state_->execution_loop()) {
      throw std::logic_error{"uv stream write used from a different loop"};
    }
    if (pin_.present() && &continuation.promise().execution_loop() != pin_.loop) {
      throw std::logic_error{"uv stream write-with-handle used from a different loop"};
    }
    if (continuation.promise().stop_requested()) {
      status_ = UV_ECANCELED;
      return false;
    }
    if (handle_submission_status_ < 0) {
      status_ = handle_submission_status_;
      return false;
    }
    auto &io = state_->io_state();
    if (io.write_active) {
      status_ = UV_EBUSY;
      return false;
    }
    if (pin_.present()) {
      assert(pin_.acquire != nullptr);
      assert(pin_.release != nullptr);
      assert(pin_.loop != nullptr);
      status_ = pin_.acquire(pin_.context);
      if (status_ < 0) {
        return false;
      }
      pin_acquired_ = true;
    }

    io.write_active = true;
    continuation_ = continuation;
    status_ = send_handle_ == nullptr
        ? uv_write(&request_, state_->stream_handle(), &buffer_, 1,
              &stream_write_awaiter::on_write)
        : uv_write2(&request_, state_->stream_handle(), &buffer_, 1, send_handle_,
              &stream_write_awaiter::on_write);
    if (status_ < 0) {
      io.write_active = false;
      release_pin();
      continuation_ = {};
      return false;
    }
    return true;
  }

  void await_resume() { throw_if_error(status_); }

private:
  static stream_write_awaiter &from_native(uv_write_t *raw) noexcept {
    return *reinterpret_cast<stream_write_awaiter *>(raw);
  }

  static void on_write(uv_write_t *raw, int status) noexcept {
    auto &self = from_native(raw);
    self.status_ = status;
    self.state_->io_state().write_active = false; // Release before resumption.
    self.release_pin();
    auto continuation = std::exchange(self.continuation_, {});
    continuation.resume();
  }

  void release_pin() noexcept {
    if (pin_acquired_) {
      pin_.release(pin_.context);
      pin_acquired_ = false;
    }
  }

  uv_write_t request_{};
  State *state_ = nullptr;
  uv_buf_t buffer_{};
  uv_stream_t *send_handle_ = nullptr;
  stream_write_pin pin_{};
  std::coroutine_handle<> continuation_{};
  int status_ = 0;
  int handle_submission_status_ = 0;
  bool pin_acquired_ = false;
};

} // namespace uv::detail
