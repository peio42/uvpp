#pragma once

#include <coroutine>
#include <exception>
#include <stdexcept>
#include <utility>

#include "uvpp/core/loop.hpp"

namespace uv::co {

namespace detail {

class task_promise_base {
public:
  void bind(uv::loop &execution_loop) noexcept { execution_loop_ = &execution_loop; }

  uv::loop &execution_loop() const {
    if (execution_loop_ == nullptr) {
      throw std::logic_error{"uv::co operation used without a spawned task"};
    }
    return *execution_loop_;
  }

private:
  uv::loop *execution_loop_ = nullptr;
};

} // namespace detail

// Experimental v3 coroutine root. A task is cold and move-only; use spawn() to
// bind it to a loop and start it.
template<class T = void>
class task;

class spawn_handle;

[[nodiscard]] spawn_handle spawn(uv::loop &, task<void> &&);

template<>
class [[nodiscard]] task<void> {
public:
  struct promise_type final : detail::task_promise_base {
    task get_return_object() noexcept {
      return task{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    void rethrow_if_failed() const {
      if (exception_ != nullptr) {
        std::rethrow_exception(exception_);
      }
    }

  private:
    using handle_type = std::coroutine_handle<promise_type>;

    std::exception_ptr exception_{};

    friend class task;
    friend class spawn_handle;
  };

  using handle_type = std::coroutine_handle<promise_type>;

  task() = delete;
  task(const task &) = delete;
  task &operator=(const task &) = delete;

  task(task &&other) noexcept : handle_{std::exchange(other.handle_, {})} {}

  task &operator=(task &&other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~task() {
    if (handle_) {
      handle_.destroy();
    }
  }

  bool valid() const noexcept { return static_cast<bool>(handle_); }

private:
  explicit task(handle_type handle) noexcept : handle_{handle} {}

  handle_type release() noexcept { return std::exchange(handle_, {}); }

  handle_type handle_{};

  friend class spawn_handle;
  friend spawn_handle spawn(uv::loop &, task &&);
};

// Experimental root-execution owner. It must outlive outstanding native work.
// Cancellation and asynchronous joining are deliberately not part of this first
// slice, so destroying an active handle terminates rather than invalidating a
// coroutine frame still referenced by libuv.
class [[nodiscard]] spawn_handle {
public:
  spawn_handle() = delete;
  spawn_handle(const spawn_handle &) = delete;
  spawn_handle &operator=(const spawn_handle &) = delete;

  spawn_handle(spawn_handle &&other) noexcept
    : handle_{std::exchange(other.handle_, {})} {}

  spawn_handle &operator=(spawn_handle &&other) noexcept {
    if (this != &other) {
      destroy_finished();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~spawn_handle() { destroy_finished(); }

  bool done() const noexcept { return handle_.done(); }

  void rethrow_if_failed() const {
    if (!done()) {
      throw std::logic_error{"uv::co::spawn_handle result observed before completion"};
    }
    handle_.promise().rethrow_if_failed();
  }

private:
  using handle_type = task<void>::handle_type;

  explicit spawn_handle(handle_type handle) noexcept : handle_{handle} {}

  void destroy_finished() noexcept {
    if (!handle_) {
      return;
    }
    if (!handle_.done()) {
      std::terminate();
    }
    handle_.destroy();
    handle_ = {};
  }

  handle_type handle_{};

  friend spawn_handle spawn(uv::loop &, task<void> &&);
};

[[nodiscard]] inline spawn_handle spawn(uv::loop &execution_loop, task<void> &&root) {
  auto handle = root.release();
  if (!handle) {
    throw std::logic_error{"uv::co::spawn requires a valid task"};
  }

  handle.promise().bind(execution_loop);
  handle.resume();
  return spawn_handle{handle};
}

} // namespace uv::co
