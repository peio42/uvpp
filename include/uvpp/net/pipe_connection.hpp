#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/detail/owner_close.hpp"
#include "uvpp/detail/stream_io.hpp"
#include "uvpp/net/buffer.hpp"
#include "uvpp/net/tcp_connection.hpp"

namespace uv {

class pipe_connection;
class pipe_connection_view;
class pipe_listener;

enum class receive_handle_kind {
  empty,
  tcp,
};

// Stable high-level TCP owners constructed directly from one native pending
// handle queue. The result deliberately does not expose its representation so
// pipe/UDP support can be added later.
class receive_handle_result {
public:
  receive_handle_result(const receive_handle_result &) = delete;
  receive_handle_result &operator=(const receive_handle_result &) = delete;
  receive_handle_result(receive_handle_result &&) noexcept = default;
  receive_handle_result &operator=(receive_handle_result &&) noexcept = default;

  receive_handle_kind kind() const noexcept {
    return tcp_.empty() ? receive_handle_kind::empty : receive_handle_kind::tcp;
  }

  std::size_t bytes_transferred() const noexcept { return bytes_transferred_; }
  std::size_t tcp_count() const noexcept { return tcp_.size(); }

  tcp_connection take_tcp() {
    if (tcp_.empty()) {
      throw std::logic_error{"received IPC handle is not TCP"};
    }
    // Keep libuv's native pending-handle order observable. Moving a
    // tcp_connection transfers its address-stable state, so erasing from this
    // small high-level result queue cannot relocate a native uv_tcp_t.
    auto result = std::move(tcp_.front());
    tcp_.erase(tcp_.begin());
    return result;
  }

private:
  explicit receive_handle_result(std::vector<tcp_connection> &&tcp,
      std::size_t bytes_transferred) noexcept
    : tcp_{std::move(tcp)},
      bytes_transferred_{bytes_transferred} {}

  std::vector<tcp_connection> tcp_{};
  std::size_t bytes_transferred_ = 0;

  friend class pipe_connection;
};

namespace detail {

// This access token is lifetime bookkeeping for a borrowed high-level view; it
// does not own the pipe resource. resource_scope clears connection before it
// destroys its sole pipe_connection owner.
struct pipe_connection_access {
  pipe_connection *connection = nullptr;
};

[[nodiscard]] pipe_connection_view make_pipe_connection_view(
    std::shared_ptr<pipe_connection_access>) noexcept;

struct pipe_connection_state {
  uv_connect_t connect{};
  uv_pipe_t pipe{};
  uv::loop *loop = nullptr;
  std::coroutine_handle<> connect_continuation{};
  std::unique_ptr<pipe_connection_state> *provisional_owner = nullptr;
  int *connect_status_destination = nullptr;
  bool *close_completion_destination = nullptr;
  int connect_status = 0;
  bool ipc = false;
  async_close_state close{};
  stream_io_state io{};

  static pipe_connection_state &from_connect(uv_connect_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<pipe_connection_state *>(
        bytes - offsetof(pipe_connection_state, connect));
  }

  static pipe_connection_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<pipe_connection_state *>(
        bytes - offsetof(pipe_connection_state, pipe));
  }

  bool closing() const noexcept { return close.closing(); }

  bool has_active_operation() const noexcept {
    return io.write_active || io.active_read != nullptr;
  }

  uv_stream_t *stream_handle() noexcept {
    return reinterpret_cast<uv_stream_t *>(&pipe);
  }

  uv::loop &execution_loop() noexcept {
    assert(loop != nullptr);
    return *loop;
  }

  stream_io_state &io_state() noexcept { return io; }

  bool request_close() noexcept {
    if (!close.begin()) {
      return false;
    }
    uv_close(reinterpret_cast<uv_handle_t *>(&pipe), &pipe_connection_state::on_close);
    return true;
  }

  // Failed connect paths originate in a libuv callback or an immediate native
  // submission result, where the fixed state-owned slot avoids allocation.
  void request_close_from_callback(std::coroutine_handle<> continuation) noexcept {
    close.set_callback_waiter(continuation);
    (void)request_close();
  }

  void release_owner() noexcept {
    close.owner_released();
    if (close.open()) {
      (void)request_close();
      return;
    }
    if (close.can_destroy_now()) {
      delete this;
    }
  }

  static void on_connect(uv_connect_t *raw, int status) noexcept {
    auto &self = from_connect(raw);
    self.connect_status = status;
    assert(self.connect_status_destination != nullptr);
    *self.connect_status_destination = status;
    auto continuation = std::exchange(self.connect_continuation, {});
    if (status < 0) {
      assert(self.provisional_owner != nullptr);
      auto *owned = self.provisional_owner->release();
      assert(owned == &self);
      self.release_owner();
      self.request_close_from_callback(continuation);
      return;
    }
    continuation.resume();
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    if (self.close_completion_destination != nullptr) {
      *self.close_completion_destination = true;
      self.close_completion_destination = nullptr;
    }
    self.close.complete([&self]() noexcept { delete &self; });
  }
};

static_assert(std::is_standard_layout_v<pipe_connection_state>);

using pipe_close_completion = owner_close_awaiter<pipe_connection_state, false>;
using pipe_close_result = owner_close_awaiter<pipe_connection_state, true>;

[[nodiscard]] pipe_close_completion close_completion(pipe_connection &) noexcept;
[[nodiscard]] pipe_close_result close_result(pipe_connection &) noexcept;

} // namespace detail

// Experimental v3 movable pipe client owner. Its single allocation keeps the
// uv_pipe_t address stable; a move transfers only that allocation. Destruction
// initiates internal close and storage survives until the native callback.
class pipe_connection {
public:
  pipe_connection(const pipe_connection &) = delete;
  pipe_connection &operator=(const pipe_connection &) = delete;

  pipe_connection(pipe_connection &&other) noexcept : state_{std::move(other.state_)} {}

  pipe_connection &operator=(pipe_connection &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  ~pipe_connection() { reset(); }

  uv_pipe_t *native() noexcept { return state_ ? &state_->pipe : nullptr; }
  const uv_pipe_t *native() const noexcept { return state_ ? &state_->pipe : nullptr; }
  uv_handle_t *native_handle() noexcept { return reinterpret_cast<uv_handle_t *>(native()); }
  const uv_handle_t *native_handle() const noexcept {
    return reinterpret_cast<const uv_handle_t *>(native());
  }
  uv_stream_t *native_stream() noexcept { return reinterpret_cast<uv_stream_t *>(native()); }
  const uv_stream_t *native_stream() const noexcept {
    return reinterpret_cast<const uv_stream_t *>(native());
  }
  bool closing() const noexcept { return state_ && state_->closing(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }
  bool has_active_operation() const noexcept {
    return state_ != nullptr && state_->has_active_operation();
  }

  [[nodiscard]] detail::pipe_close_completion close() & noexcept {
    return detail::pipe_close_completion{state_.get(), true};
  }
  detail::pipe_close_completion close() && = delete;

  void request_close() {
    if (!state_) {
      throw_if_error(UV_EBADF);
    }
    if (!state_->close.closed() && state_->has_active_operation()) {
      throw_if_error(UV_EBUSY);
    }
    (void)state_->request_close();
  }

  using read_some_result = detail::stream_read_some_result;
  using read_awaiter = detail::stream_read_awaiter<detail::pipe_connection_state>;

  // Borrows buffer storage until data, EOF, error, or cancellation has stopped
  // the native read and released both callback slots.
  [[nodiscard]] read_awaiter read_some(std::span<std::byte> buffer) noexcept {
    return read_awaiter{state_.get(), buffer};
  }

  using write_awaiter = detail::stream_write_awaiter<detail::pipe_connection_state>;

  // Borrows data until the native write completion invokes the awaiting task.
  [[nodiscard]] write_awaiter write(std::string_view data) {
    detail::check_buffer_length(data.size());
    return write_awaiter{state_.get(), data};
  }

  // Borrows both the bytes and the TCP owner through uv_write2() completion.
  // This passes a native capability; it does not move the source C++ owner.
  [[nodiscard]] write_awaiter write_with_handle(
      std::string_view data, tcp_connection &handle) {
    detail::check_buffer_length(data.size());
    if (state_ == nullptr || handle.state_ == nullptr) {
      return write_awaiter{state_.get(), data, nullptr, {}, UV_EBADF};
    }
    auto *source = handle.state_.get();
    const detail::stream_write_pin pin{
        source,
        source->loop,
        &detail::tcp_connection_state::acquire_handle_export,
        &detail::tcp_connection_state::release_handle_export};
    return write_awaiter{state_.get(), data, source->stream_handle(), pin,
        state_->ipc ? 0 : UV_EINVAL};
  }

  class receive_handle_awaiter {
  public:
    receive_handle_awaiter(detail::pipe_connection_state *state,
        std::span<std::byte> buffer) noexcept
      : state_{state}, buffer_{buffer} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->closing()) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != state_->loop) {
        throw std::logic_error{"uv::pipe_connection receive_handle used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (!state_->ipc) {
        status_ = UV_EINVAL;
        return false;
      }
      if (state_->io.active_read != nullptr) {
        status_ = UV_EBUSY;
        return false;
      }
      if (buffer_.empty()) {
        status_ = UV_EINVAL;
        return false;
      }

      // C callbacks are noexcept. Reserve one future stable TCP owner before
      // starting the native source. A burst may require further no-throw
      // allocations in the callback; allocation failure becomes UV_ENOMEM and
      // closes the pipe rather than escaping the native callback.
      incoming_ = std::make_unique<detail::tcp_connection_state>();
      incoming_->loop = state_->loop;
      continuation_ = continuation;

      state_->io.active_read = this;
      status_ = uv_read_start(state_->stream_handle(), &receive_handle_awaiter::on_alloc,
          &receive_handle_awaiter::on_read);
      if (status_ < 0) {
        state_->io.active_read = nullptr;
        continuation_ = {};
        return false;
      }
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &receive_handle_awaiter::on_stop_requested, this)) {
        stop_native_or_terminate();
        release_slots();
        continuation_ = {};
        status_ = UV_ECANCELED;
        return false;
      }
      return true;
    }

    receive_handle_result await_resume() {
      assert(!provisional_close_required_ || provisional_close_completed_);
      assert(!received_.empty() || status_ < 0);
      throw_if_error(status_);
      return receive_handle_result{std::move(received_), bytes_transferred_};
    }

  private:
    static receive_handle_awaiter &active(uv_handle_t *raw) noexcept {
      auto &state = detail::pipe_connection_state::from_handle(raw);
      assert(state.io.active_read != nullptr);
      return *static_cast<receive_handle_awaiter *>(state.io.active_read);
    }

    static void on_alloc(uv_handle_t *raw, size_t, uv_buf_t *out) noexcept {
      auto &self = active(raw);
      *out = detail::make_native_buffer_unchecked(
          reinterpret_cast<char *>(self.buffer_.data() + self.bytes_transferred_),
          self.buffer_.size() - self.bytes_transferred_);
    }

    static void on_read(uv_stream_t *raw, ssize_t nread, const uv_buf_t *) noexcept {
      auto &self = active(reinterpret_cast<uv_handle_t *>(raw));
      self.status_ = nread;
      if (nread > 0) {
        self.bytes_transferred_ += static_cast<std::size_t>(nread);
        // Pipe reads are a byte stream: an nread notification is not an IPC
        // message boundary. Preserve every byte read while waiting for a
        // pending handle, but do not associate a particular subset with the
        // adopted owner.
        if (!self.adopt_pending_tcp()) {
          if (self.bytes_transferred_ == self.buffer_.size()) {
            self.status_ = UV_ENOBUFS;
          } else {
            return;
          }
        }
      } else if (nread == 0) {
        // Do not rely on this being possible, but consume a pending handle if
        // libuv reports one without accompanying bytes.
        if (!self.adopt_pending_tcp()) {
          return;
        }
      }
      self.stop_native_or_terminate();
      self.release_slots();
      auto continuation = std::exchange(self.continuation_, {});
      if (self.status_ < 0 && self.incoming_ != nullptr && self.incoming_->initialized) {
        if (self.close_pipe_) {
          self.close_pipe_after_protocol_failure();
        }
        self.discard_received();
        self.close_untransferred_connection(continuation);
        return;
      }
      if (self.status_ < 0 && self.close_pipe_) {
        self.close_pipe_after_protocol_failure();
      }
      if (self.status_ < 0) {
        self.discard_received();
      }
      continuation.resume();
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<receive_handle_awaiter *>(context);
      if (self.state_ == nullptr || self.state_->io.active_read != &self) {
        return;
      }
      self.status_ = UV_ECANCELED;
      self.stop_native_or_terminate();
      self.release_slots();
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }

    // Drains every pending native handle visible in this stream notification.
    // A single receive result carries the resulting owners; accepting only one
    // here can lose the notification needed to observe its queued siblings.
    bool adopt_pending_tcp() noexcept {
      bool adopted = false;
      for (;;) {
        const int count = uv_pipe_pending_count(&state_->pipe);
        if (count < 0) {
          status_ = count;
          close_pipe_ = true;
          return true;
        }
        if (count == 0) {
          status_ = 0;
          return adopted;
        }
        if (uv_pipe_pending_type(&state_->pipe) != UV_TCP) {
          status_ = UV_EPROTO;
          close_pipe_ = true;
          return true;
        }
        if (received_.capacity() < received_.size() + static_cast<std::size_t>(count)) {
          try {
            received_.reserve(received_.size() + static_cast<std::size_t>(count));
          } catch (...) {
            status_ = UV_ENOMEM;
            close_pipe_ = true;
            return true;
          }
        }

        std::unique_ptr<detail::tcp_connection_state> candidate;
        if (incoming_ != nullptr) {
          candidate = std::move(incoming_);
        } else {
          try {
            candidate = std::make_unique<detail::tcp_connection_state>();
          } catch (...) {
            status_ = UV_ENOMEM;
            close_pipe_ = true;
            return true;
          }
          candidate->loop = state_->loop;
        }

        status_ = uv_tcp_init(state_->loop->native(), &candidate->tcp);
        if (status_ < 0) {
          close_pipe_ = true;
          incoming_ = std::move(candidate);
          return true;
        }
        candidate->initialized = true;
        status_ = uv_accept(state_->stream_handle(), candidate->stream_handle());
        if (status_ < 0) {
          close_pipe_ = true;
          incoming_ = std::move(candidate);
          return true;
        }
        // reserve() above made this move-only emplacement non-allocating.
        received_.emplace_back(tcp_connection{std::move(candidate)});
        adopted = true;
      }
    }

    // The TCP-only result cannot consume a pending handle of another native
    // family. Fail closed so it cannot remain as a poison entry which a later
    // receive_handle() would observe with unspecified semantics.
    void close_pipe_after_protocol_failure() noexcept {
      assert(close_pipe_ && status_ < 0);
      if (state_ != nullptr && !state_->closing()) {
        (void)state_->request_close();
      }
    }

    void discard_received() noexcept {
      received_.clear();
    }

    void close_untransferred_connection(std::coroutine_handle<> continuation) noexcept {
      auto *connection = incoming_.release();
      assert(connection != nullptr);
      provisional_close_required_ = true;
      connection->close_completion_destination = &provisional_close_completed_;
      connection->release_owner();
      connection->request_close_from_callback(continuation);
    }

    void stop_native_or_terminate() noexcept {
      const int status = uv_read_stop(state_->stream_handle());
      assert(status >= 0);
      if (status < 0) {
        std::terminate();
      }
    }

    void release_slots() noexcept {
      if (state_ != nullptr && state_->io.active_read == this) {
        state_->io.active_read = nullptr;
      }
      if (cancellation_ != nullptr) {
        cancellation_->unregister(cancellation_registration_);
        cancellation_ = nullptr;
      }
    }

    detail::pipe_connection_state *state_ = nullptr;
    std::span<std::byte> buffer_{};
    std::unique_ptr<detail::tcp_connection_state> incoming_{};
    std::vector<tcp_connection> received_{};
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    ssize_t status_ = 0;
    std::size_t bytes_transferred_ = 0;
    bool provisional_close_required_ = false;
    bool provisional_close_completed_ = false;
    bool close_pipe_ = false;
  };

  // Receives stream bytes until a notification exposes at least one pending
  // TCP handle, then drains that native pending queue into stable TCP owners.
  // libuv exposes a byte stream, not a framing relationship between those
  // bytes and native handles: buffer accumulates every byte seen while waiting,
  // but the returned count must not be associated with a particular owner. If
  // the buffer fills first, the operation fails with UV_ENOBUFS. take_tcp()
  // extracts one owner at a time from the returned queue. An unsupported pending
  // type fails closed with UV_EPROTO.
  [[nodiscard]] receive_handle_awaiter receive_handle(std::span<std::byte> buffer) noexcept {
    return receive_handle_awaiter{state_.get(), buffer};
  }

  class connect_awaiter {
  public:
    connect_awaiter(std::string_view name, bool ipc) : name_{name}, ipc_{ipc} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }

      state_ = std::make_unique<detail::pipe_connection_state>();
      auto &state = *state_;
      state.loop = &continuation.promise().execution_loop();
      state.connect_continuation = continuation;
      state.provisional_owner = &state_;
      state.connect_status_destination = &status_;
      state.ipc = ipc_;
      status_ = uv_pipe_init(state.loop->native(), &state.pipe, ipc_ ? 1 : 0);
      state.connect_status = status_;
      if (status_ < 0) {
        return false;
      }

#if UVPP_HAS_PIPE_CONNECT2
      status_ = uv_pipe_connect2(&state.connect, &state.pipe, name_.data(), name_.size(), 0,
          &detail::pipe_connection_state::on_connect);
      state.connect_status = status_;
      if (status_ < 0) {
        auto resume = std::exchange(state.connect_continuation, {});
        auto *owned = state_.release();
        assert(owned == &state);
        state.release_owner();
        state.request_close_from_callback(resume);
      }
#else
      // The legacy API reports all connect failures asynchronously. name_ is
      // awaiter-owned, so its NUL-terminated storage remains valid through
      // initiation without borrowing the caller's string_view.
      uv_pipe_connect(&state.connect, &state.pipe, name_.c_str(),
          &detail::pipe_connection_state::on_connect);
#endif
      return true;
    }

    pipe_connection await_resume() {
      throw_if_error(status_);
      state_->provisional_owner = nullptr;
      state_->connect_status_destination = nullptr;
      return pipe_connection{std::move(state_)};
    }

  private:
    std::string name_{};
    bool ipc_ = false;
    std::unique_ptr<detail::pipe_connection_state> state_{};
    int status_ = 0;
  };

  // Connects to a local pipe name. The name is copied during awaiter creation;
  // `ipc` only selects libuv's handle-passing mode and does not expose an IPC
  // transfer API in this initial coroutine slice.
  [[nodiscard]] static connect_awaiter connect(std::string_view name, bool ipc = false) {
    return connect_awaiter{name, ipc};
  }

private:
  explicit pipe_connection(std::unique_ptr<detail::pipe_connection_state> state) noexcept
    : state_{std::move(state)} {}

  void reset() noexcept {
    if (!state_) {
      return;
    }
    auto *state = state_.release();
    assert(!state->io.write_active);
    assert(state->io.active_read == nullptr);
    if (state->io.write_active || state->io.active_read != nullptr) {
      std::terminate();
    }
    state->release_owner();
  }

  std::unique_ptr<detail::pipe_connection_state> state_{};

  friend class pipe_connection_view;
  friend class pipe_listener;
  friend detail::pipe_close_completion detail::close_completion(pipe_connection &) noexcept;
  friend detail::pipe_close_result detail::close_result(pipe_connection &) noexcept;
};

// A non-owning pipe facade produced by resource_scope. It retains only an
// access token and diagnoses use after resource cleanup before native access.
class pipe_connection_view {
public:
  [[nodiscard]] pipe_connection::read_awaiter read_some(std::span<std::byte> buffer) const {
    return connection().read_some(buffer);
  }

  [[nodiscard]] pipe_connection::write_awaiter write(std::string_view data) const {
    return connection().write(data);
  }

  [[nodiscard]] pipe_connection::write_awaiter write_with_handle(
      std::string_view data, tcp_connection &handle) const {
    return connection().write_with_handle(data, handle);
  }

  [[nodiscard]] pipe_connection::receive_handle_awaiter receive_handle(
      std::span<std::byte> buffer) const {
    return connection().receive_handle(buffer);
  }

private:
  explicit pipe_connection_view(std::shared_ptr<detail::pipe_connection_access> access) noexcept
    : access_{std::move(access)} {}

  pipe_connection &connection() const {
    if (!access_ || access_->connection == nullptr) {
      throw std::logic_error{"uv::pipe_connection_view used after resource cleanup"};
    }
    return *access_->connection;
  }

  std::shared_ptr<detail::pipe_connection_access> access_{};

  friend pipe_connection_view detail::make_pipe_connection_view(
      std::shared_ptr<detail::pipe_connection_access>) noexcept;
};

namespace detail {

[[nodiscard]] inline pipe_connection_view make_pipe_connection_view(
    std::shared_ptr<pipe_connection_access> access) noexcept {
  return pipe_connection_view{std::move(access)};
}

[[nodiscard]] inline pipe_close_completion close_completion(
    pipe_connection &connection) noexcept {
  return pipe_close_completion{connection.state_.get(), false};
}

[[nodiscard]] inline pipe_close_result close_result(pipe_connection &connection) noexcept {
  return pipe_close_result{connection.state_.get(), true};
}

} // namespace detail

namespace ops {

[[nodiscard]] inline detail::pipe_close_result close(pipe_connection &connection) noexcept {
  return detail::close_result(connection);
}

} // namespace ops

static_assert(std::is_standard_layout_v<pipe_connection::write_awaiter>);

} // namespace uv
