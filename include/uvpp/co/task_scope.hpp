#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "uvpp/co/task.hpp"

namespace uv::co {

// Experimental structured owner for same-loop task<void> children. It starts
// children immediately, retains their frames through completion, and requires
// co_await join() before destruction. The first child failure requests
// cooperative stop for its siblings; join still waits for every child.
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
    ++completion_depth_;
    assert(!child.completed);
    child.completed = true;
    assert(active_children_ != 0);
    --active_children_;

    bool first_failure = false;
    if (first_failure_ == nullptr) {
      try {
        child.handle.promise().rethrow_if_failed();
      } catch (...) {
        first_failure_ = std::current_exception();
        first_failure = true;
      }
    }
    if (first_failure) {
      cancellation_.request_stop();
    }

    --completion_depth_;
    // Stop callbacks may complete sibling tasks synchronously. Do not resume a
    // joining parent until the outermost completion callback has unwound; it may
    // then safely destroy all child frames in join_awaiter's await_resume().
    if (active_children_ == 0 && join_continuation_ && completion_depth_ == 0) {
      return std::exchange(join_continuation_, {});
    }
    return std::noop_coroutine();
  }

  void finish_join() {
    assert(active_children_ == 0);
    assert(completion_depth_ == 0);
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
  std::size_t completion_depth_ = 0;
  bool join_started_ = false;
};

} // namespace uv::co
