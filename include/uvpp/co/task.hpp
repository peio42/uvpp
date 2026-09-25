#pragma once

#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "uvpp/co/cancellation.hpp"
#include "uvpp/core/loop.hpp"

namespace uv::co {

namespace detail {

class task_promise_base {
public:
  void bind(uv::loop &execution_loop, cancellation_state *cancellation = nullptr) {
    if (execution_loop_ != nullptr) {
      throw std::logic_error{"uv::co task has already been started"};
    }
    execution_loop_ = &execution_loop;
    cancellation_ = cancellation;
  }

  uv::loop &execution_loop() const {
    if (execution_loop_ == nullptr) {
      throw std::logic_error{"uv::co operation used without a spawned task"};
    }
    return *execution_loop_;
  }

  cancellation_state *cancellation() const noexcept { return cancellation_; }
  bool stop_requested() const noexcept {
    return cancellation_ != nullptr && cancellation_->stop_requested();
  }

  void set_continuation(std::coroutine_handle<> continuation) noexcept {
    continuation_ = continuation;
  }

  std::coroutine_handle<> continuation() const noexcept { return continuation_; }

  using completion_callback = std::coroutine_handle<> (*)(void *) noexcept;

  void set_completion(void *context, completion_callback callback) noexcept {
    completion_context_ = context;
    completion_callback_ = callback;
  }

  std::coroutine_handle<> complete() noexcept {
    return completion_callback_ ? completion_callback_(completion_context_) : std::noop_coroutine();
  }

private:
  uv::loop *execution_loop_ = nullptr;
  cancellation_state *cancellation_ = nullptr;
  std::coroutine_handle<> continuation_{};
  void *completion_context_ = nullptr;
  completion_callback completion_callback_ = nullptr;
};

struct task_final_awaiter {
  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, task_promise_base>
  std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> completed) const noexcept {
    auto continuation = completed.promise().continuation();
    return continuation ? continuation : completed.promise().complete();
  }

  void await_resume() const noexcept {}
};

} // namespace detail

// Experimental v3 coroutine root or child. A task is cold and move-only; use
// spawn() for a root or co_await a temporary task from another task.
template<class T = void>
class task;

template<class T = void>
class spawn_handle;
class task_scope;

template<class T>
[[nodiscard]] spawn_handle<T> spawn(uv::loop &, task<T> &&);

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

    void rethrow_if_failed() const {
      if (exception_ != nullptr) {
        std::rethrow_exception(exception_);
      }
    }

    bool has_failure() const noexcept { return exception_ != nullptr; }

    bool has_result() const noexcept { return value_.has_value(); }

    T take_result() {
      rethrow_if_failed();
      if (!value_) {
        throw std::logic_error{"uv::co task completed without a value"};
      }
      auto result = std::move(*value_);
      value_.reset();
      return result;
    }

  private:
    using handle_type = std::coroutine_handle<promise_type>;

    std::optional<T> value_{};
    std::exception_ptr exception_{};

    friend class task;
    template<class U>
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
      child_promise.bind(parent.promise().execution_loop(), parent.promise().cancellation());
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

  template<class U>
  friend spawn_handle<U> spawn(uv::loop &, task<U> &&);
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

    bool has_failure() const noexcept { return exception_ != nullptr; }

  private:
    using handle_type = std::coroutine_handle<promise_type>;

    std::exception_ptr exception_{};

    friend class task;
    template<class U>
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
      child_promise.bind(parent.promise().execution_loop(), parent.promise().cancellation());
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

  template<class U>
  friend class spawn_handle;
  friend class task_scope;
  template<class U>
  friend spawn_handle<U> spawn(uv::loop &, task<U> &&);
};

class stop_requested_awaiter {
public:
  bool await_ready() const noexcept { return false; }

  template<class Promise>
    requires std::derived_from<Promise, detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) noexcept {
    requested_ = continuation.promise().stop_requested();
    return false;
  }

  bool await_resume() const noexcept { return requested_; }

private:
  bool requested_ = false;
};

// Observes the cooperative stop state inherited by the current task.
[[nodiscard]] inline stop_requested_awaiter stop_requested() noexcept { return {}; }

// Experimental root-execution observer. It supplies the root cancellation state
// inherited by descendants and keeps completion observation separate from
// single-consumer value extraction. The running root owns an internal completion
// baton, so destroying this public handle requests stop without invalidating a
// coroutine frame still referenced by libuv.
template<class T>
class [[nodiscard]] spawn_handle {
private:
  struct state;
  class completion_baton;

public:
  spawn_handle() = delete;
  spawn_handle(const spawn_handle &) = delete;
  spawn_handle &operator=(const spawn_handle &) = delete;

  spawn_handle(spawn_handle &&other) noexcept
    : state_{std::move(other.state_)} {}

  spawn_handle &operator=(spawn_handle &&other) noexcept {
    if (this != &other) {
      release();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  ~spawn_handle() { release(); }

  bool done() const noexcept { return state_ != nullptr && state_->completed; }

  void rethrow_if_failed() const {
    completed_state().handle.promise().rethrow_if_failed();
  }

  // Requests cooperative stop for this root and all nested children. It is
  // loop-thread-only and does not mean submitted native work has completed.
  void request_stop() { require_state().cancellation.request_stop(); }

  bool stop_requested() const { return require_state().cancellation.stop_requested(); }

  class join_awaiter {
  public:
    explicit join_awaiter(std::shared_ptr<state> state) noexcept
      : state_{std::move(state)} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (&continuation.promise().execution_loop() != state_->loop) {
        throw std::logic_error{"uv::co::spawn_handle joined from a different loop"};
      }
      if (state_->completed) {
        return false;
      }
      state_->joiners.push_back(continuation);
      return true;
    }

    void await_resume() const noexcept {}

  private:
    std::shared_ptr<state> state_;
  };

  // Completion is observable by multiple same-loop tasks. Constructing this
  // awaiter has no effect; it registers only when actually awaited.
  [[nodiscard]] join_awaiter join() const {
    return join_awaiter{require_state_ptr()};
  }

private:
  using handle_type = task<T>::handle_type;

  struct state {
    explicit state(handle_type root, uv::loop &execution_loop) noexcept
      : handle{root}, loop{&execution_loop} {}

    ~state() {
      if (!handle) {
        return;
      }
      if (!handle.done()) {
        std::terminate();
      }
      handle.destroy();
    }

    static std::coroutine_handle<> on_completed(void *context) noexcept {
      auto &self = *static_cast<state *>(context);
      // The completion baton holds a shared reference to this state. Returning
      // it transfers out of the root final-suspend protocol before completion
      // delivery can release the last execution owner and destroy the root.
      return std::exchange(self.baton, std::coroutine_handle<>{});
    }

    void deliver_completion() noexcept {
      completed = true;

      // Detach every user continuation before resuming any of them. A resumed
      // joiner may otherwise observe callback-frame references still owned by
      // this completion delivery.
      auto completed_joiners = std::move(joiners);
      this->joiners.clear();
      for (auto joiner : completed_joiners) {
        joiner.resume();
      }

      // A failure after the sole public handle was abandoned has no result
      // observer. Keep the existing diagnostic rather than silently discarding
      // it; configurable unobserved-failure routing is a later slice.
      if (public_handle_abandoned && handle.promise().has_failure()) {
        std::terminate();
      }
    }

    handle_type handle{};
    uv::loop *loop = nullptr;
    detail::cancellation_state cancellation{};
    std::vector<std::coroutine_handle<>> joiners{};
    std::coroutine_handle<> baton{};
    bool completed = false;
    bool public_handle_abandoned = false;
  };

  // This suspended coroutine is created before the root starts and holds the
  // execution reference independently of the public spawn_handle. The root
  // transfers to it from final_suspend; therefore its final reference may
  // destroy the root only after that root is safely suspended.
  class completion_baton {
  public:
    struct promise_type {
      completion_baton get_return_object() noexcept {
        return completion_baton{handle_type::from_promise(*this)};
      }

      std::suspend_always initial_suspend() noexcept { return {}; }
      std::suspend_never final_suspend() noexcept { return {}; }
      void return_void() noexcept {}
      void unhandled_exception() noexcept { std::terminate(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    completion_baton() = default;
    completion_baton(const completion_baton &) = delete;
    completion_baton &operator=(const completion_baton &) = delete;

    completion_baton(completion_baton &&other) noexcept
      : handle_{std::exchange(other.handle_, {})} {}

    std::coroutine_handle<> release() noexcept {
      return std::exchange(handle_, {});
    }

    static completion_baton make(std::shared_ptr<state> retained) {
      retained->deliver_completion();
      co_return;
    }

  private:
    explicit completion_baton(handle_type handle) noexcept : handle_{handle} {}

    handle_type handle_{};
  };

public:
  bool has_result() const noexcept
    requires (!std::is_void_v<T>) {
    return state_ != nullptr && state_->completed && state_->handle.promise().has_result();
  }

  T take_result()
    requires (!std::is_void_v<T>) {
    return completed_state().handle.promise().take_result();
  }

private:
  explicit spawn_handle(std::shared_ptr<state> state) noexcept : state_{std::move(state)} {}

  state &require_state() const {
    if (state_ == nullptr) {
      throw std::logic_error{"uv::co::spawn_handle is empty"};
    }
    return *state_;
  }

  std::shared_ptr<state> require_state_ptr() const {
    (void)require_state();
    return state_;
  }

  state &completed_state() const {
    auto &result = require_state();
    if (!result.completed) {
      throw std::logic_error{"uv::co::spawn_handle result observed before completion"};
    }
    return result;
  }

  void release() noexcept {
    auto released = std::move(state_);
    if (released == nullptr) {
      return;
    }
    if (!released->completed) {
      // Keep a local reference while request_stop() invokes callbacks: a stop
      // callback may synchronously complete the root and run the baton.
      released->public_handle_abandoned = true;
      released->cancellation.request_stop();
    }
  }

  std::shared_ptr<state> state_{};

  template<class U>
  friend spawn_handle<U> spawn(uv::loop &, task<U> &&);
};

template<class T>
[[nodiscard]] inline spawn_handle<T> spawn(uv::loop &execution_loop, task<T> &&root) {
  auto handle = root.release();
  if (!handle) {
    throw std::logic_error{"uv::co::spawn requires a valid task"};
  }

  std::shared_ptr<typename spawn_handle<T>::state> state;
  try {
    state = std::make_shared<typename spawn_handle<T>::state>(handle, execution_loop);
    state->baton = spawn_handle<T>::completion_baton::make(state).release();
    handle.promise().bind(execution_loop, &state->cancellation);
    handle.promise().set_completion(state.get(), &spawn_handle<T>::state::on_completed);
  } catch (...) {
    if (state != nullptr) {
      if (state->baton) {
        state->baton.destroy();
        state->baton = {};
      }
      state->handle.destroy();
      state->handle = {};
    } else {
      handle.destroy();
    }
    throw;
  }
  handle.resume();
  return spawn_handle<T>{std::move(state)};
}

} // namespace uv::co
