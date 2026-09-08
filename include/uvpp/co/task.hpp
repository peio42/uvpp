#pragma once

#include <concepts>
#include <coroutine>
#include <cassert>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "uvpp/core/loop.hpp"

namespace uv::co {

namespace detail {

class cancellation_state;

class cancellation_registration {
public:
  cancellation_registration() = default;
  cancellation_registration(const cancellation_registration &) = delete;
  cancellation_registration &operator=(const cancellation_registration &) = delete;

private:
  using callback = void (*)(void *) noexcept;

  cancellation_state *state_ = nullptr;
  cancellation_registration *next_ = nullptr;
  callback callback_ = nullptr;
  void *context_ = nullptr;

  friend class cancellation_state;
};

// Loop-thread-only cooperative cancellation. Registrations are removed before
// their callback runs, so an operation may resume and destroy its frame safely.
class cancellation_state {
public:
  bool stop_requested() const noexcept { return stop_requested_; }

  bool register_callback(cancellation_registration &registration,
      cancellation_registration::callback callback, void *context) noexcept {
    assert(registration.state_ == nullptr);
    if (stop_requested_) {
      return false;
    }
    registration.state_ = this;
    registration.callback_ = callback;
    registration.context_ = context;
    registration.next_ = head_;
    head_ = &registration;
    return true;
  }

  void unregister(cancellation_registration &registration) noexcept {
    if (registration.state_ == nullptr) {
      return;
    }
    assert(registration.state_ == this);
    auto **current = &head_;
    while (*current != &registration) {
      current = &(*current)->next_;
    }
    *current = registration.next_;
    clear(registration);
  }

  void request_stop() noexcept {
    if (std::exchange(stop_requested_, true)) {
      return;
    }
    while (head_ != nullptr) {
      auto &registration = *head_;
      head_ = registration.next_;
      auto callback = registration.callback_;
      auto *context = registration.context_;
      clear(registration);
      callback(context);
    }
  }

private:
  static void clear(cancellation_registration &registration) noexcept {
    registration.state_ = nullptr;
    registration.next_ = nullptr;
    registration.callback_ = nullptr;
    registration.context_ = nullptr;
  }

  cancellation_registration *head_ = nullptr;
  bool stop_requested_ = false;
};

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

class spawn_handle;
class task_scope;

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

  friend class spawn_handle;
  friend class task_scope;
  friend spawn_handle spawn(uv::loop &, task &&);
};

// Experimental structured owner for same-loop task<void> children. It starts
// children immediately, retains their frames through completion, and requires
// co_await join() before destruction. Cancellation and resource cleanup are
// deliberately deferred to the following scope slice.
class [[nodiscard]] task_scope {
public:
  explicit task_scope(uv::loop &execution_loop) noexcept : loop_{&execution_loop} {}

  task_scope(const task_scope &) = delete;
  task_scope &operator=(const task_scope &) = delete;
  task_scope(task_scope &&) = delete;
  task_scope &operator=(task_scope &&) = delete;

  ~task_scope() {
    assert(children_.empty());
    if (!children_.empty()) {
      std::terminate();
    }
  }

  void spawn(task<void> &&child) {
    if (join_started_) {
      throw std::logic_error{"uv::co::task_scope cannot spawn after join"};
    }

    auto handle = child.release();
    if (!handle) {
      throw std::logic_error{"uv::co::task_scope requires a valid task"};
    }

    auto record = std::make_unique<child_record>();
    record->scope = this;
    record->handle = handle;
    try {
      handle.promise().bind(*loop_, &cancellation_);
      handle.promise().set_completion(record.get(), &task_scope::on_child_completed);
      children_.push_back(std::move(record));
      ++active_children_;
      handle.resume();
    } catch (...) {
      if (record != nullptr && record->handle) {
        record->handle.destroy();
      }
      throw;
    }
  }

  class join_awaiter {
  public:
    explicit join_awaiter(task_scope &scope) noexcept : scope_{scope} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (&continuation.promise().execution_loop() != scope_.loop_) {
        throw std::logic_error{"uv::co::task_scope joined from a different loop"};
      }
      if (scope_.join_started_) {
        throw std::logic_error{"uv::co::task_scope can be joined only once"};
      }
      scope_.join_started_ = true;
      if (scope_.active_children_ == 0) {
        return false;
      }
      scope_.join_continuation_ = continuation;
      return true;
    }

    void await_resume() { scope_.finish_join(); }

  private:
    task_scope &scope_;
  };

  [[nodiscard]] join_awaiter join() noexcept { return join_awaiter{*this}; }

  // Requests cooperative stop for all children. Completion and join remain
  // asynchronous: non-cancellable native work retains its storage until done.
  void request_stop() noexcept { cancellation_.request_stop(); }
  bool stop_requested() const noexcept { return cancellation_.stop_requested(); }

private:
  using handle_type = task<void>::handle_type;

  struct child_record {
    task_scope *scope = nullptr;
    handle_type handle{};
    bool completed = false;
  };

  static std::coroutine_handle<> on_child_completed(void *context) noexcept {
    auto &child = *static_cast<child_record *>(context);
    return child.scope->child_completed(child);
  }

  std::coroutine_handle<> child_completed(child_record &child) noexcept {
    assert(!child.completed);
    child.completed = true;
    assert(active_children_ != 0);
    --active_children_;
    if (first_failure_ == nullptr) {
      try {
        child.handle.promise().rethrow_if_failed();
      } catch (...) {
        first_failure_ = std::current_exception();
      }
    }
    if (active_children_ == 0 && join_continuation_) {
      return std::exchange(join_continuation_, {});
    }
    return std::noop_coroutine();
  }

  void finish_join() {
    assert(active_children_ == 0);
    for (auto &child : children_) {
      assert(child->completed);
      child->handle.destroy();
    }
    children_.clear();
    if (first_failure_ != nullptr) {
      std::rethrow_exception(std::exchange(first_failure_, {}));
    }
  }

  uv::loop *loop_ = nullptr;
  detail::cancellation_state cancellation_{};
  std::vector<std::unique_ptr<child_record>> children_{};
  std::coroutine_handle<> join_continuation_{};
  std::exception_ptr first_failure_{};
  std::size_t active_children_ = 0;
  bool join_started_ = false;
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
