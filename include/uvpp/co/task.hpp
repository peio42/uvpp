#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

#include "uvpp/core/loop.hpp"

namespace uv::co {

namespace detail {

class task_promise_base {
public:
  void bind(uv::loop &execution_loop) {
    if (execution_loop_ != nullptr) {
      throw std::logic_error{"uv::co task has already been started"};
    }
    execution_loop_ = &execution_loop;
  }

  uv::loop &execution_loop() const {
    if (execution_loop_ == nullptr) {
      throw std::logic_error{"uv::co operation used without a spawned task"};
    }
    return *execution_loop_;
  }

  void set_continuation(std::coroutine_handle<> continuation) noexcept {
    continuation_ = continuation;
  }

  std::coroutine_handle<> continuation() const noexcept { return continuation_; }

private:
  uv::loop *execution_loop_ = nullptr;
  std::coroutine_handle<> continuation_{};
};

struct task_final_awaiter {
  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, task_promise_base>
  std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> completed) const noexcept {
    auto continuation = completed.promise().continuation();
    return continuation ? continuation : std::noop_coroutine();
  }

  void await_resume() const noexcept {}
};

} // namespace detail

// Experimental v3 coroutine root or child. A task is cold and move-only; use
// spawn() for a root or co_await a temporary task from another task.
template<class T = void>
class task;

class spawn_handle;

[[nodiscard]] spawn_handle spawn(uv::loop &, task<void> &&);

template<class T>
class [[nodiscard]] task {
  static_assert(std::move_constructible<T>, "uv::co::task values must be move constructible");

public:
  struct promise_type final : detail::task_promise_base {
    task get_return_object() noexcept {
      return task{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    detail::task_final_awaiter final_suspend() noexcept { return {}; }

    template<class U>
      requires std::constructible_from<T, U &&>
    void return_value(U &&value) {
      value_.emplace(std::forward<U>(value));
    }

    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    T take_result() {
      if (exception_ != nullptr) {
        std::rethrow_exception(exception_);
      }
      if (!value_) {
        throw std::logic_error{"uv::co task completed without a value"};
      }
      return std::move(*value_);
    }

  private:
    using handle_type = std::coroutine_handle<promise_type>;

    std::optional<T> value_{};
    std::exception_ptr exception_{};

    friend class task;
  };

  using handle_type = std::coroutine_handle<promise_type>;

  task() = delete;
  task(const task &) = delete;
  task &operator=(const task &) = delete;

  task(task &&other) noexcept : handle_{std::exchange(other.handle_, {})} {}

  task &operator=(task &&other) noexcept {
    if (this != &other) {
      destroy_cold();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~task() { destroy_cold(); }

  bool valid() const noexcept { return static_cast<bool>(handle_); }

  class awaiter {
  public:
    explicit awaiter(handle_type child) noexcept : child_{child} {}
    awaiter(const awaiter &) = delete;
    awaiter &operator=(const awaiter &) = delete;
    awaiter(awaiter &&other) noexcept : child_{std::exchange(other.child_, {})} {}
    awaiter &operator=(awaiter &&) = delete;

    ~awaiter() { destroy_child(); }

    bool await_ready() const noexcept { return false; }

    template<class ParentPromise>
      requires std::derived_from<ParentPromise, detail::task_promise_base>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> parent) {
      auto &child_promise = child_.promise();
      child_promise.bind(parent.promise().execution_loop());
      child_promise.set_continuation(parent);
      return child_;
    }

    T await_resume() {
      auto child = std::exchange(child_, {});
      try {
        auto value = child.promise().take_result();
        child.destroy();
        return value;
      } catch (...) {
        child.destroy();
        throw;
      }
    }

  private:
    void destroy_child() noexcept {
      if (!child_) {
        return;
      }
      if (!child_.done()) {
        std::terminate();
      }
      child_.destroy();
      child_ = {};
    }

    handle_type child_{};
  };

  [[nodiscard]] awaiter operator co_await() && {
    if (!handle_) {
      throw std::logic_error{"uv::co::task can be awaited only once"};
    }
    return awaiter{release()};
  }

private:
  explicit task(handle_type handle) noexcept : handle_{handle} {}

  handle_type release() noexcept { return std::exchange(handle_, {}); }

  void destroy_cold() noexcept {
    if (handle_) {
      handle_.destroy();
      handle_ = {};
    }
  }

  handle_type handle_{};
};

template<>
class [[nodiscard]] task<void> {
public:
  struct promise_type final : detail::task_promise_base {
    task get_return_object() noexcept {
      return task{handle_type::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    detail::task_final_awaiter final_suspend() noexcept { return {}; }
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
      destroy_cold();
      handle_ = std::exchange(other.handle_, {});
    }
    return *this;
  }

  ~task() { destroy_cold(); }

  bool valid() const noexcept { return static_cast<bool>(handle_); }

  class awaiter {
  public:
    explicit awaiter(handle_type child) noexcept : child_{child} {}
    awaiter(const awaiter &) = delete;
    awaiter &operator=(const awaiter &) = delete;
    awaiter(awaiter &&other) noexcept : child_{std::exchange(other.child_, {})} {}
    awaiter &operator=(awaiter &&) = delete;

    ~awaiter() { destroy_child(); }

    bool await_ready() const noexcept { return false; }

    template<class ParentPromise>
      requires std::derived_from<ParentPromise, detail::task_promise_base>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<ParentPromise> parent) {
      auto &child_promise = child_.promise();
      child_promise.bind(parent.promise().execution_loop());
      child_promise.set_continuation(parent);
      return child_;
    }

    void await_resume() {
      auto child = std::exchange(child_, {});
      try {
        child.promise().rethrow_if_failed();
        child.destroy();
      } catch (...) {
        child.destroy();
        throw;
      }
    }

  private:
    void destroy_child() noexcept {
      if (!child_) {
        return;
      }
      if (!child_.done()) {
        std::terminate();
      }
      child_.destroy();
      child_ = {};
    }

    handle_type child_{};
  };

  [[nodiscard]] awaiter operator co_await() && {
    if (!handle_) {
      throw std::logic_error{"uv::co::task can be awaited only once"};
    }
    return awaiter{release()};
  }

private:
  explicit task(handle_type handle) noexcept : handle_{handle} {}

  handle_type release() noexcept { return std::exchange(handle_, {}); }

  void destroy_cold() noexcept {
    if (handle_) {
      handle_.destroy();
      handle_ = {};
    }
  }

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
