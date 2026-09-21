#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <uv.h>

#include "uvpp/co/cancellation.hpp"
#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/native.hpp"

namespace uv::co {
class resource_scope;
}

namespace uv::fs {

class file;
class file_view;

namespace detail {

struct close_waiter {
  close_waiter *next = nullptr;
  void *context = nullptr;
  void (*complete)(void *, int) noexcept = nullptr;
};

// The descriptor has no native libuv handle to retain.  Its owner and every
// submitted operation share this stable heap state instead.  It never uses
// uv_req_t::data: each request reconstructs its awaiting operation from its
// own address-stable uv_fs_t storage.
struct file_state {
  uv::loop *loop = nullptr;
  uv_file descriptor = -1;
  close_waiter *close_waiters = nullptr;
  int close_status = 0;
  bool read_active = false;
  bool write_active = false;
  bool close_active = false;
  bool close_complete = false;

  bool open() const noexcept { return descriptor >= 0 && !close_active && !close_complete; }
  bool has_active_operation() const noexcept {
    return read_active || write_active || close_active;
  }
};

struct file_access;

template<bool> class detail_open_awaiter;
template<bool> class detail_read_awaiter;
template<bool> class detail_write_awaiter;
template<bool> class detail_close_awaiter;

std::shared_ptr<file_state> file_state_for(file &) noexcept;
std::shared_ptr<file_state> file_state_for(const file_view &);

} // namespace detail

// Typed one-shot filesystem read completion.  EOF is distinct from a
// zero-sized caller buffer, whose result has count() == 0 and eof() == false.
class file_read_result {
public:
  file_read_result(std::size_t count, bool eof) noexcept : count_{count}, eof_{eof} {}

  std::size_t count() const noexcept { return count_; }
  bool eof() const noexcept { return eof_; }

private:
  std::size_t count_ = 0;
  bool eof_ = false;
};

// Movable owner of one opened filesystem descriptor.  Its state allocation is
// made before uv_fs_open() is submitted, so a successful native open always
// has a stable owner.  Destruction diagnoses an unclosed file or borrowed I/O;
// use close() or resource_scope::finish() to observe cleanup.
class file {
public:
  file() = delete;
  file(const file &) = delete;
  file &operator=(const file &) = delete;
  file(file &&) noexcept = default;
  file &operator=(file &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  ~file() { reset(); }

  uv_file native() const noexcept { return state_ ? state_->descriptor : -1; }
  bool is_open() const noexcept { return state_ != nullptr && state_->open(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }
  bool has_active_operation() const noexcept {
    return state_ != nullptr && state_->has_active_operation();
  }

private:
  explicit file(std::shared_ptr<detail::file_state> state) noexcept : state_{std::move(state)} {}

  void reset() noexcept {
    if (!state_) {
      return;
    }
    // A close awaiter retains state through its terminal callback, so dropping
    // an owner while close is pending is safe.  Any unclosed descriptor or
    // borrowed read/write remains a visible lifecycle violation.
    if (state_->read_active || state_->write_active || state_->open()) {
      assert(false && "uv::fs::file must be closed after borrowed I/O settles");
      std::terminate();
    }
    state_.reset();
  }

  std::shared_ptr<detail::file_state> state_;

  friend class file_view;
  friend struct detail::file_access;
  template<bool> friend class detail::detail_open_awaiter;
  template<bool> friend class detail::detail_read_awaiter;
  template<bool> friend class detail::detail_write_awaiter;
  template<bool> friend class detail::detail_close_awaiter;
  friend std::shared_ptr<detail::file_state> detail::file_state_for(file &) noexcept;
};

namespace detail {

struct file_access {
  file *owner = nullptr;
};

} // namespace detail

// A scope-bound borrowed file capability.  resource_scope invalidates it before
// destroying its owner; it never provides independent close authority.
class file_view {
public:
  file_view() = default;

private:
  explicit file_view(std::shared_ptr<detail::file_access> access) noexcept
    : access_{std::move(access)} {}

  std::shared_ptr<detail::file_state> state() const {
    if (!access_ || access_->owner == nullptr) {
      throw std::logic_error{"uv::fs::file_view used after resource cleanup"};
    }
    return access_->owner->state_;
  }

  std::shared_ptr<detail::file_access> access_;

  friend class uv::co::resource_scope;
  template<bool> friend class detail::detail_read_awaiter;
  template<bool> friend class detail::detail_write_awaiter;
  friend std::shared_ptr<detail::file_state> detail::file_state_for(const file_view &);
};

namespace detail {

inline std::shared_ptr<file_state> file_state_for(file &owner) noexcept {
  return owner.state_;
}

inline std::shared_ptr<file_state> file_state_for(const file_view &view) {
  return view.state();
}

template<bool ExplicitResult>
class detail_open_awaiter final
  : private uv::detail::native_storage<detail_open_awaiter<ExplicitResult>, uv_fs_t, uv_req_t> {
  using storage_type = uv::detail::native_storage<detail_open_awaiter<ExplicitResult>, uv_fs_t, uv_req_t>;
  friend storage_type;

public:
  detail_open_awaiter(std::string_view path, int flags, int mode)
    : state_{std::make_shared<file_state>()}, path_{path}, flags_{flags}, mode_{mode} {}

  detail_open_awaiter(const detail_open_awaiter &) = delete;
  detail_open_awaiter &operator=(const detail_open_awaiter &) = delete;
  detail_open_awaiter(detail_open_awaiter &&) = delete;
  detail_open_awaiter &operator=(detail_open_awaiter &&) = delete;

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    auto &promise = continuation.promise();
    if (promise.stop_requested()) {
      status_ = UV_ECANCELED;
      return false;
    }

    state_->loop = &promise.execution_loop();
    continuation_ = continuation;
    status_ = uv_fs_open(state_->loop->native(), this->native(), path_.c_str(), flags_, mode_,
      &detail_open_awaiter::on_complete);
    if (status_ < 0) {
      continuation_ = {};
      return false;
    }
    submitted_ = true;
    cancellation_ = promise.cancellation();
    if (cancellation_ != nullptr && !cancellation_->register_callback(
        cancellation_registration_, &detail_open_awaiter::on_stop_requested, this)) {
      request_native_cancellation();
    }
    return true;
  }

  auto await_resume() {
    if constexpr (ExplicitResult) {
      if (status_ < 0) {
        return uv::result<file>{make_error_code(static_cast<int>(status_))};
      }
      return uv::result<file>{file{std::move(state_)}};
    } else {
      throw_if_error(status_);
      return file{std::move(state_)};
    }
  }

private:
  static void on_complete(uv_fs_t *raw) noexcept {
    auto &self = storage_type::from_native(raw);
    self.complete(uv_fs_get_result(raw));
  }

  static void on_stop_requested(void *context) noexcept {
    static_cast<detail_open_awaiter *>(context)->request_native_cancellation();
  }

  void request_native_cancellation() noexcept {
    if (submitted_) {
      (void)uv_cancel(this->native_base());
    }
  }

  void complete(ssize_t status) noexcept {
    if (cancellation_ != nullptr) {
      cancellation_->unregister(cancellation_registration_);
      cancellation_ = nullptr;
    }
    submitted_ = false;
    status_ = status;
    if (status >= 0) {
      state_->descriptor = static_cast<uv_file>(status);
    }
    uv_fs_req_cleanup(this->native());
    path_.clear();
    auto continuation = std::exchange(continuation_, {});
    continuation.resume();
  }

  std::shared_ptr<file_state> state_;
  std::string path_;
  std::coroutine_handle<> continuation_{};
  co::detail::cancellation_state *cancellation_ = nullptr;
  co::detail::cancellation_registration cancellation_registration_{};
  int flags_ = 0;
  int mode_ = 0;
  ssize_t status_ = 0;
  bool submitted_ = false;
};

template<bool ExplicitResult>
class detail_read_awaiter final
  : private uv::detail::native_storage<detail_read_awaiter<ExplicitResult>, uv_fs_t, uv_req_t> {
  using storage_type = uv::detail::native_storage<detail_read_awaiter<ExplicitResult>, uv_fs_t, uv_req_t>;
  friend storage_type;

public:
  detail_read_awaiter(std::shared_ptr<file_state> state, std::span<std::byte> buffer,
                      std::int64_t offset) noexcept
    : state_{std::move(state)}, buffer_{reinterpret_cast<char *>(buffer.data()), buffer.size()},
      offset_{offset}, empty_{buffer.empty()} {}

  detail_read_awaiter(const detail_read_awaiter &) = delete;
  detail_read_awaiter &operator=(const detail_read_awaiter &) = delete;
  detail_read_awaiter(detail_read_awaiter &&) = delete;
  detail_read_awaiter &operator=(detail_read_awaiter &&) = delete;

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (!state_ || !state_->open()) {
      status_ = UV_EBADF;
      return false;
    }
    auto &promise = continuation.promise();
    if (&promise.execution_loop() != state_->loop) {
      throw std::logic_error{"uv::fs::read used from a different loop"};
    }
    if (promise.stop_requested()) {
      status_ = UV_ECANCELED;
      return false;
    }
    if (empty_) {
      return false;
    }
    if (state_->read_active) {
      status_ = UV_EBUSY;
      return false;
    }

    state_->read_active = true;
    continuation_ = continuation;
    status_ = uv_fs_read(state_->loop->native(), this->native(), state_->descriptor, &buffer_, 1,
      offset_, &detail_read_awaiter::on_complete);
    if (status_ < 0) {
      state_->read_active = false;
      continuation_ = {};
      return false;
    }
    submitted_ = true;
    cancellation_ = promise.cancellation();
    if (cancellation_ != nullptr && !cancellation_->register_callback(
        cancellation_registration_, &detail_read_awaiter::on_stop_requested, this)) {
      request_native_cancellation();
    }
    return true;
  }

  auto await_resume() {
    if constexpr (ExplicitResult) {
      if (status_ < 0) {
        return uv::result<file_read_result>{make_error_code(static_cast<int>(status_))};
      }
      return uv::result<file_read_result>{file_read_result{static_cast<std::size_t>(status_),
          !empty_ && status_ == 0}};
    } else {
      throw_if_error(status_);
      return file_read_result{static_cast<std::size_t>(status_), !empty_ && status_ == 0};
    }
  }

private:
  static void on_complete(uv_fs_t *raw) noexcept {
    auto &self = storage_type::from_native(raw);
    self.complete(uv_fs_get_result(raw));
  }

  static void on_stop_requested(void *context) noexcept {
    static_cast<detail_read_awaiter *>(context)->request_native_cancellation();
  }

  void request_native_cancellation() noexcept {
    if (submitted_) {
      (void)uv_cancel(this->native_base());
    }
  }

  void complete(ssize_t status) noexcept {
    if (cancellation_ != nullptr) {
      cancellation_->unregister(cancellation_registration_);
      cancellation_ = nullptr;
    }
    submitted_ = false;
    status_ = status;
    state_->read_active = false;
    uv_fs_req_cleanup(this->native());
    auto continuation = std::exchange(continuation_, {});
    continuation.resume();
  }

  std::shared_ptr<file_state> state_;
  uv_buf_t buffer_{};
  std::coroutine_handle<> continuation_{};
  co::detail::cancellation_state *cancellation_ = nullptr;
  co::detail::cancellation_registration cancellation_registration_{};
  std::int64_t offset_ = 0;
  ssize_t status_ = 0;
  bool empty_ = false;
  bool submitted_ = false;
};

template<bool ExplicitResult>
class detail_write_awaiter final
  : private uv::detail::native_storage<detail_write_awaiter<ExplicitResult>, uv_fs_t, uv_req_t> {
  using storage_type = uv::detail::native_storage<detail_write_awaiter<ExplicitResult>, uv_fs_t, uv_req_t>;
  friend storage_type;

public:
  detail_write_awaiter(std::shared_ptr<file_state> state, std::span<const std::byte> buffer,
                       std::int64_t offset) noexcept
    : state_{std::move(state)}, buffer_{const_cast<char *>(reinterpret_cast<const char *>(buffer.data())),
          buffer.size()}, offset_{offset}, empty_{buffer.empty()} {}

  detail_write_awaiter(const detail_write_awaiter &) = delete;
  detail_write_awaiter &operator=(const detail_write_awaiter &) = delete;
  detail_write_awaiter(detail_write_awaiter &&) = delete;
  detail_write_awaiter &operator=(detail_write_awaiter &&) = delete;

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (!state_ || !state_->open()) {
      status_ = UV_EBADF;
      return false;
    }
    auto &promise = continuation.promise();
    if (&promise.execution_loop() != state_->loop) {
      throw std::logic_error{"uv::fs::write used from a different loop"};
    }
    if (promise.stop_requested()) {
      status_ = UV_ECANCELED;
      return false;
    }
    if (empty_) {
      return false;
    }
    if (state_->write_active) {
      status_ = UV_EBUSY;
      return false;
    }

    state_->write_active = true;
    continuation_ = continuation;
    status_ = uv_fs_write(state_->loop->native(), this->native(), state_->descriptor, &buffer_, 1,
      offset_, &detail_write_awaiter::on_complete);
    if (status_ < 0) {
      state_->write_active = false;
      continuation_ = {};
      return false;
    }
    submitted_ = true;
    cancellation_ = promise.cancellation();
    if (cancellation_ != nullptr && !cancellation_->register_callback(
        cancellation_registration_, &detail_write_awaiter::on_stop_requested, this)) {
      request_native_cancellation();
    }
    return true;
  }

  auto await_resume() {
    if constexpr (ExplicitResult) {
      if (status_ < 0) {
        return uv::result<std::size_t>{make_error_code(static_cast<int>(status_))};
      }
      return uv::result<std::size_t>{static_cast<std::size_t>(status_)};
    } else {
      throw_if_error(status_);
      return static_cast<std::size_t>(status_);
    }
  }

private:
  static void on_complete(uv_fs_t *raw) noexcept {
    auto &self = storage_type::from_native(raw);
    self.complete(uv_fs_get_result(raw));
  }

  static void on_stop_requested(void *context) noexcept {
    static_cast<detail_write_awaiter *>(context)->request_native_cancellation();
  }

  void request_native_cancellation() noexcept {
    if (submitted_) {
      (void)uv_cancel(this->native_base());
    }
  }

  void complete(ssize_t status) noexcept {
    if (cancellation_ != nullptr) {
      cancellation_->unregister(cancellation_registration_);
      cancellation_ = nullptr;
    }
    submitted_ = false;
    status_ = status;
    state_->write_active = false;
    uv_fs_req_cleanup(this->native());
    auto continuation = std::exchange(continuation_, {});
    continuation.resume();
  }

  std::shared_ptr<file_state> state_;
  uv_buf_t buffer_{};
  std::coroutine_handle<> continuation_{};
  co::detail::cancellation_state *cancellation_ = nullptr;
  co::detail::cancellation_registration cancellation_registration_{};
  std::int64_t offset_ = 0;
  ssize_t status_ = 0;
  bool empty_ = false;
  bool submitted_ = false;
};

template<bool ExplicitResult>
class detail_close_awaiter final
  : private uv::detail::native_storage<detail_close_awaiter<ExplicitResult>, uv_fs_t, uv_req_t> {
  using storage_type = uv::detail::native_storage<detail_close_awaiter<ExplicitResult>, uv_fs_t, uv_req_t>;
  friend storage_type;

public:
  explicit detail_close_awaiter(std::shared_ptr<file_state> state) noexcept : state_{std::move(state)} {}

  detail_close_awaiter(const detail_close_awaiter &) = delete;
  detail_close_awaiter &operator=(const detail_close_awaiter &) = delete;
  detail_close_awaiter(detail_close_awaiter &&) = delete;
  detail_close_awaiter &operator=(detail_close_awaiter &&) = delete;

  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (!state_) {
      status_ = UV_EBADF;
      return false;
    }
    if (&continuation.promise().execution_loop() != state_->loop) {
      throw std::logic_error{"uv::fs::close used from a different loop"};
    }
    if (state_->close_complete) {
      status_ = state_->close_status;
      return false;
    }
    continuation_ = continuation;
    waiter_ = {nullptr, this, &detail_close_awaiter::complete_waiter};
    if (state_->close_active) {
      add_waiter();
      return true;
    }
    if (!state_->open()) {
      continuation_ = {};
      status_ = UV_EBADF;
      return false;
    }
    if (state_->read_active || state_->write_active) {
      continuation_ = {};
      status_ = UV_EBUSY;
      return false;
    }

    state_->close_active = true;
    add_waiter();
    status_ = uv_fs_close(state_->loop->native(), this->native(), state_->descriptor,
      &detail_close_awaiter::on_complete);
    if (status_ < 0) {
      assert(state_->close_waiters == &waiter_);
      state_->close_waiters = nullptr;
      state_->close_active = false;
      continuation_ = {};
      return false;
    }
    return true;
  }

  auto await_resume() {
    if constexpr (ExplicitResult) {
      return uv::status::from_native(status_);
    } else {
      throw_if_error(status_);
    }
  }

private:
  static void on_complete(uv_fs_t *raw) noexcept {
    auto &self = storage_type::from_native(raw);
    // Copy state before any user coroutine can resume. A resumed close waiter
    // may destroy its own frame, including `self`, so this trampoline must not
    // execute a member function or touch request storage after delivery starts.
    auto state = self.state_;
    const auto status = static_cast<int>(uv_fs_get_result(raw));
    uv_fs_req_cleanup(raw);
    state->descriptor = -1;
    state->close_active = false;
    state->close_complete = true;
    state->close_status = status;
    auto *waiter = std::exchange(state->close_waiters, nullptr);
    while (waiter != nullptr) {
      auto *next = waiter->next;
      waiter->complete(waiter->context, status);
      waiter = next;
    }
  }

  static void complete_waiter(void *context, int status) noexcept {
    auto &self = *static_cast<detail_close_awaiter *>(context);
    self.status_ = status;
    auto continuation = std::exchange(self.continuation_, {});
    continuation.resume();
  }

  void add_waiter() noexcept {
    waiter_.next = state_->close_waiters;
    state_->close_waiters = &waiter_;
  }

  std::shared_ptr<file_state> state_;
  std::coroutine_handle<> continuation_{};
  close_waiter waiter_{};
  int status_ = 0;
};

} // namespace detail

// The throwing filesystem surface.  `open` owns copied path storage through
// completion. `read` and `write` borrow their buffer until actual completion.
[[nodiscard]] inline detail::detail_open_awaiter<false> open(
    std::string_view path, int flags, int mode) {
  return detail::detail_open_awaiter<false>{path, flags, mode};
}

[[nodiscard]] inline detail::detail_read_awaiter<false> read(
    file &owner, std::span<std::byte> buffer, std::int64_t offset = -1) noexcept {
  return detail::detail_read_awaiter<false>{detail::file_state_for(owner), buffer, offset};
}

[[nodiscard]] inline detail::detail_read_awaiter<false> read(
    const file_view &owner, std::span<std::byte> buffer, std::int64_t offset = -1) {
  return detail::detail_read_awaiter<false>{detail::file_state_for(owner), buffer, offset};
}

[[nodiscard]] inline detail::detail_write_awaiter<false> write(
    file &owner, std::span<const std::byte> buffer, std::int64_t offset = -1) noexcept {
  return detail::detail_write_awaiter<false>{detail::file_state_for(owner), buffer, offset};
}

[[nodiscard]] inline detail::detail_write_awaiter<false> write(
    const file_view &owner, std::span<const std::byte> buffer, std::int64_t offset = -1) {
  return detail::detail_write_awaiter<false>{detail::file_state_for(owner), buffer, offset};
}

[[nodiscard]] inline detail::detail_close_awaiter<false> close(file &owner) noexcept {
  return detail::detail_close_awaiter<false>{detail::file_state_for(owner)};
}

} // namespace uv::fs

namespace uv::ops::fs {

using uv::fs::file;
using uv::fs::file_view;
namespace detail = uv::fs::detail;

[[nodiscard]] inline detail::detail_open_awaiter<true> open(
    std::string_view path, int flags, int mode) {
  return detail::detail_open_awaiter<true>{path, flags, mode};
}

[[nodiscard]] inline detail::detail_read_awaiter<true> read(
    file &owner, std::span<std::byte> buffer, std::int64_t offset = -1) noexcept {
  return detail::detail_read_awaiter<true>{detail::file_state_for(owner), buffer, offset};
}

[[nodiscard]] inline detail::detail_read_awaiter<true> read(
    const file_view &owner, std::span<std::byte> buffer, std::int64_t offset = -1) {
  return detail::detail_read_awaiter<true>{detail::file_state_for(owner), buffer, offset};
}

[[nodiscard]] inline detail::detail_write_awaiter<true> write(
    file &owner, std::span<const std::byte> buffer, std::int64_t offset = -1) noexcept {
  return detail::detail_write_awaiter<true>{detail::file_state_for(owner), buffer, offset};
}

[[nodiscard]] inline detail::detail_write_awaiter<true> write(
    const file_view &owner, std::span<const std::byte> buffer, std::int64_t offset = -1) {
  return detail::detail_write_awaiter<true>{detail::file_state_for(owner), buffer, offset};
}

[[nodiscard]] inline detail::detail_close_awaiter<true> close(file &owner) noexcept {
  return detail::detail_close_awaiter<true>{detail::file_state_for(owner)};
}

} // namespace uv::ops::fs
