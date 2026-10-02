#pragma once

#include <algorithm>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"

namespace uv {

class loop_posting_endpoint;

namespace detail {

class loop_posting_state final : public std::enable_shared_from_this<loop_posting_state> {
public:
  using failure_handler = std::function<void(std::exception_ptr)>;

  enum class phase {
    open,
    draining,
    closing,
    closed,
  };

  explicit loop_posting_state(uv::loop &execution_loop, std::size_t capacity,
                              std::size_t drain_budget)
    : loop{&execution_loop}, capacity_{capacity}, drain_budget_{drain_budget} {
    if (capacity == 0 || drain_budget == 0) {
      throw std::invalid_argument{"uv::loop_posting capacity and drain budget must be nonzero"};
    }
    batch_.reserve(std::min(capacity, drain_budget));
  }

  loop_posting_state(const loop_posting_state &) = delete;
  loop_posting_state &operator=(const loop_posting_state &) = delete;

  ~loop_posting_state() {
    // The native handle may be destroyed only after its close callback. The
    // public owner diagnoses a missing close; this catches an endpoint that
    // happened to retain the state until after that violation.
    if (initialized_ && phase_ != phase::closed) {
      std::terminate();
    }
  }

  void init() {
    throw_if_error(uv_async_init(loop->native(), &async_, &on_async));
    async_.data = this;
    initialized_ = true;
  }

  template<class F>
    requires std::invocable<std::decay_t<F> &>
  status post(F &&callable) {
    auto work = std::make_unique<work_model<std::decay_t<F>>>(std::forward<F>(callable));
    std::unique_ptr<work_base> rejected;
    int native_status = 0;
    {
      std::lock_guard lock{mutex_};
      if (phase_ != phase::open) {
        native_status = UV_ECANCELED;
      } else if (queue_.size() == capacity_) {
        native_status = UV_EAGAIN;
      } else {
        queue_.push_back(std::move(work));
        native_status = uv_async_send(&async_);
        if (native_status < 0) {
          rejected = std::move(queue_.back());
          queue_.pop_back();
        }
      }
    }
    return status::from_native(native_status);
  }

  void set_failure_handler(failure_handler handler) {
    failure_handler_.replace(std::move(handler));
  }

  void request_close() {
    std::lock_guard lock{mutex_};
    if (phase_ == phase::open) {
      phase_ = phase::draining;
    }
    begin_close_if_drained_locked();
  }

  bool closed() const {
    std::lock_guard lock{mutex_};
    return phase_ == phase::closed;
  }

  bool belongs_to(const uv::loop &execution_loop) const noexcept {
    return loop == &execution_loop;
  }

  bool add_close_waiter(std::coroutine_handle<> continuation) {
    std::lock_guard lock{mutex_};
    if (phase_ == phase::closed) {
      return false;
    }
    close_waiters_.push_back(continuation);
    return true;
  }

private:
  struct work_base {
    virtual ~work_base() = default;
    virtual void invoke() = 0;
  };

  template<class F>
  struct work_model final : work_base {
    template<class G>
    explicit work_model(G &&callable) : callable_{std::forward<G>(callable)} {}
    void invoke() override { callable_(); }
    F callable_;
  };

  static loop_posting_state &from_native(uv_async_t *raw) noexcept {
    return *static_cast<loop_posting_state *>(raw->data);
  }

  static void on_async(uv_async_t *raw) noexcept { from_native(raw).drain(); }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_native(reinterpret_cast<uv_async_t *>(raw));
    // Resumed close waiters may release both the public owner and their awaiters.
    auto retained = self.shared_from_this();
    std::vector<std::coroutine_handle<>> waiters;
    {
      std::lock_guard lock{self.mutex_};
      self.phase_ = phase::closed;
      waiters = std::move(self.close_waiters_);
    }
    self.failure_handler_.replace({});
    for (auto waiter : waiters) {
      waiter.resume();
    }
  }

  void drain() noexcept {
    {
      std::lock_guard lock{mutex_};
      if (phase_ == phase::closing || phase_ == phase::closed) {
        return;
      }
      const auto count = std::min(drain_budget_, queue_.size());
      for (std::size_t i = 0; i != count; ++i) {
        batch_.push_back(std::move(queue_.front()));
        queue_.pop_front();
      }
      if (!batch_.empty()) {
        ++active_batches_;
      }
    }

    const bool had_work = !batch_.empty();
    for (auto &work : batch_) {
      try {
        work->invoke();
      } catch (...) {
        report_failure(std::current_exception());
      }
    }
    // Captures may publish or request close from their destructors. Keep this
    // batch active until destruction finishes, and never destroy them locked.
    batch_.clear();

    {
      std::lock_guard lock{mutex_};
      if (had_work) {
        --active_batches_;
      }
      if (!queue_.empty()) {
        if (uv_async_send(&async_) < 0) {
          std::terminate();
        }
      }
      begin_close_if_drained_locked();
    }
  }

  void report_failure(std::exception_ptr failure) noexcept {
    const bool handled = failure_handler_.invoke(
        [&](failure_handler &handler) { handler(failure); });
    if (!handled) {
      std::terminate();
    }
  }

  void begin_close_if_drained_locked() noexcept {
    if (phase_ != phase::draining || !queue_.empty() || active_batches_ != 0) {
      return;
    }
    phase_ = phase::closing;
    uv_close(reinterpret_cast<uv_handle_t *>(&async_), &on_close);
  }

  uv_async_t async_{};
  uv::loop *loop = nullptr;
  mutable std::mutex mutex_{};
  std::deque<std::unique_ptr<work_base>> queue_{};
  std::vector<std::unique_ptr<work_base>> batch_{};
  std::vector<std::coroutine_handle<>> close_waiters_{};
  persistent_callback_slot<failure_handler> failure_handler_{};
  std::size_t capacity_ = 0;
  std::size_t drain_budget_ = 0;
  std::size_t active_batches_ = 0;
  phase phase_ = phase::open;
  bool initialized_ = false;
};

} // namespace detail

// Experimental explicit cross-thread publication facility. Its owner is
// loop-thread-affine; endpoints are the only producer-thread capability.
class loop_posting final {
public:
  using failure_handler = detail::loop_posting_state::failure_handler;

  explicit loop_posting(uv::loop &loop, std::size_t capacity = 1024,
                        std::size_t drain_budget = 64)
    : state_{std::make_shared<detail::loop_posting_state>(loop, capacity, drain_budget)} {
    state_->init();
  }

  loop_posting(const loop_posting &) = delete;
  loop_posting &operator=(const loop_posting &) = delete;
  loop_posting(loop_posting &&) = delete;
  loop_posting &operator=(loop_posting &&) = delete;

  ~loop_posting() {
    if (state_ && !state_->closed()) {
      std::terminate();
    }
  }

  [[nodiscard]] loop_posting_endpoint endpoint() const noexcept;

  void set_failure_handler(failure_handler handler) {
    state_->set_failure_handler(std::move(handler));
  }

  // Starts the drain-and-close transition. This must be called on the loop
  // thread; completion is observed through close().
  void request_close() { state_->request_close(); }

  bool closed() const { return state_->closed(); }

  class close_awaiter {
  public:
    explicit close_awaiter(std::shared_ptr<detail::loop_posting_state> state) noexcept
      : state_{std::move(state)} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (!state_->belongs_to(continuation.promise().execution_loop())) {
        throw std::logic_error{"uv::loop_posting closed from a different loop"};
      }
      if (!state_->add_close_waiter(continuation)) {
        return false;
      }
      state_->request_close();
      return true;
    }

    void await_resume() const noexcept {}

  private:
    std::shared_ptr<detail::loop_posting_state> state_;
  };

  [[nodiscard]] close_awaiter close() const noexcept { return close_awaiter{state_}; }

private:
  std::shared_ptr<detail::loop_posting_state> state_;
};

// A copyable producer capability. It intentionally has no native-handle or
// loop accessor, and may be passed to threads that do not own the loop.
class loop_posting_endpoint final {
public:
  loop_posting_endpoint() = delete;

  template<class F>
    requires std::invocable<std::decay_t<F> &>
  void post(F &&callable) const {
    auto posted = post_result(std::forward<F>(callable));
    posted.value();
  }

  template<class F>
    requires std::invocable<std::decay_t<F> &>
  status post_result(F &&callable) const {
    return state_->post(std::forward<F>(callable));
  }

private:
  explicit loop_posting_endpoint(std::shared_ptr<detail::loop_posting_state> state) noexcept
    : state_{std::move(state)} {}

  std::shared_ptr<detail::loop_posting_state> state_;

  friend class loop_posting;
};

inline loop_posting_endpoint loop_posting::endpoint() const noexcept {
  return loop_posting_endpoint{state_};
}

namespace ops {

template<class F>
  requires std::invocable<std::decay_t<F> &>
status post(const loop_posting_endpoint &endpoint, F &&callable) {
  return endpoint.post_result(std::forward<F>(callable));
}

} // namespace ops

} // namespace uv
