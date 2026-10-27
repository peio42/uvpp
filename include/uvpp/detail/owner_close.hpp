#pragma once

#include <concepts>
#include <coroutine>
#include <stdexcept>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"

namespace uv::detail {

// Shared awaiter for a high-level owner's native close transition. The State
// retains the native handle, family-specific quiescence, and the close callback;
// it must provide `loop`, `close`, `has_active_operation()`, and
// `request_close()`.
template<class State, bool ExplicitResult>
class owner_close_awaiter {
public:
  explicit owner_close_awaiter(State *state, bool reject_active_operations) noexcept
    : state_{state}, reject_active_operations_{reject_active_operations} {}

  owner_close_awaiter(const owner_close_awaiter &) = delete;
  owner_close_awaiter &operator=(const owner_close_awaiter &) = delete;
  owner_close_awaiter(owner_close_awaiter &&) = delete;
  owner_close_awaiter &operator=(owner_close_awaiter &&) = delete;

  // Affinity is checked at suspension even for an already closed owner.
  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (state_ == nullptr) {
      status_ = UV_EBADF;
      return false;
    }
    if (&continuation.promise().execution_loop() != state_->loop) {
      throw std::logic_error{"uv owner close used from a different loop"};
    }
    if (state_->close.closed()) {
      return false;
    }
    if (reject_active_operations_ && state_->has_active_operation()) {
      status_ = UV_EBUSY;
      return false;
    }
    if (!state_->close.add_waiter(continuation)) {
      return false;
    }
    initiated_ = state_->request_close();
    return true;
  }

  auto await_resume() {
    if constexpr (ExplicitResult) {
      return uv::status::from_native(status_);
    } else {
      throw_if_error(status_);
    }
  }

  bool initiated_close() const noexcept { return initiated_; }

private:
  State *state_ = nullptr;
  int status_ = 0;
  bool reject_active_operations_ = false;
  bool initiated_ = false;
};

} // namespace uv::detail
