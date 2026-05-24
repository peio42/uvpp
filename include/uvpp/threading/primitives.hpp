#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/version.hpp"

namespace uv {

  class mutex final {
  public:
    using raw_type = uv_mutex_t;

    mutex() {
      throw_if_error(uv_mutex_init(&raw_));
    }

    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;
    mutex(mutex&&) = delete;
    mutex& operator=(mutex&&) = delete;

    ~mutex() {
      uv_mutex_destroy(&raw_);
    }

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    void lock() {
      uv_mutex_lock(&raw_);
    }

    bool try_lock() {
      const int status = uv_mutex_trylock(&raw_);
      if (status == 0) {
        return true;
      }
      if (status == UV_EBUSY) {
        return false;
      }
      throw error(status);
    }

    void unlock() noexcept {
      uv_mutex_unlock(&raw_);
    }

  private:
    raw_type raw_{};
  };

  class recursive_mutex final {
  public:
    using raw_type = uv_mutex_t;

    recursive_mutex() {
      throw_if_error(uv_mutex_init_recursive(&raw_));
    }

    recursive_mutex(const recursive_mutex&) = delete;
    recursive_mutex& operator=(const recursive_mutex&) = delete;
    recursive_mutex(recursive_mutex&&) = delete;
    recursive_mutex& operator=(recursive_mutex&&) = delete;

    ~recursive_mutex() {
      uv_mutex_destroy(&raw_);
    }

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    void lock() {
      uv_mutex_lock(&raw_);
    }

    bool try_lock() {
      const int status = uv_mutex_trylock(&raw_);
      if (status == 0) {
        return true;
      }
      if (status == UV_EBUSY) {
        return false;
      }
      throw error(status);
    }

    void unlock() noexcept {
      uv_mutex_unlock(&raw_);
    }

  private:
    raw_type raw_{};
  };

  class rwlock final {
  public:
    using raw_type = uv_rwlock_t;

    rwlock() {
      throw_if_error(uv_rwlock_init(&raw_));
    }

    rwlock(const rwlock&) = delete;
    rwlock& operator=(const rwlock&) = delete;
    rwlock(rwlock&&) = delete;
    rwlock& operator=(rwlock&&) = delete;

    ~rwlock() {
      uv_rwlock_destroy(&raw_);
    }

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    void lock_read() {
      uv_rwlock_rdlock(&raw_);
    }

    bool try_lock_read() {
      const int status = uv_rwlock_tryrdlock(&raw_);
      if (status == 0) {
        return true;
      }
      if (status == UV_EBUSY) {
        return false;
      }
      throw error(status);
    }

    void unlock_read() noexcept {
      uv_rwlock_rdunlock(&raw_);
    }

    void lock_write() {
      uv_rwlock_wrlock(&raw_);
    }

    bool try_lock_write() {
      const int status = uv_rwlock_trywrlock(&raw_);
      if (status == 0) {
        return true;
      }
      if (status == UV_EBUSY) {
        return false;
      }
      throw error(status);
    }

    void unlock_write() noexcept {
      uv_rwlock_wrunlock(&raw_);
    }

  private:
    raw_type raw_{};
  };

  class semaphore final {
  public:
    using raw_type = uv_sem_t;

    explicit semaphore(unsigned int value = 0) {
      throw_if_error(uv_sem_init(&raw_, value));
    }

    semaphore(const semaphore&) = delete;
    semaphore& operator=(const semaphore&) = delete;
    semaphore(semaphore&&) = delete;
    semaphore& operator=(semaphore&&) = delete;

    ~semaphore() {
      uv_sem_destroy(&raw_);
    }

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    void post() noexcept {
      uv_sem_post(&raw_);
    }

    void wait() {
      uv_sem_wait(&raw_);
    }

    bool try_wait() {
      const int status = uv_sem_trywait(&raw_);
      if (status == 0) {
        return true;
      }
      if (status == UV_EAGAIN) {
        return false;
      }
      throw error(status);
    }

  private:
    raw_type raw_{};
  };

  class condition_variable final {
  public:
    using raw_type = uv_cond_t;

    condition_variable() {
      throw_if_error(uv_cond_init(&raw_));
    }

    condition_variable(const condition_variable&) = delete;
    condition_variable& operator=(const condition_variable&) = delete;
    condition_variable(condition_variable&&) = delete;
    condition_variable& operator=(condition_variable&&) = delete;

    ~condition_variable() {
      uv_cond_destroy(&raw_);
    }

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    void signal() noexcept {
      uv_cond_signal(&raw_);
    }

    void broadcast() noexcept {
      uv_cond_broadcast(&raw_);
    }

    void wait(mutex &lock) noexcept {
      uv_cond_wait(&raw_, lock.native());
    }

    template<class Rep, class Period>
    bool wait_for(mutex &lock, std::chrono::duration<Rep, Period> timeout) {
      const int status = uv_cond_timedwait(&raw_, lock.native(), nanoseconds(timeout));
      if (status == 0) {
        return true;
      }
      if (status == UV_ETIMEDOUT) {
        return false;
      }
      throw error(status);
    }

  private:
    template<class Rep, class Period>
    static uint64_t nanoseconds(std::chrono::duration<Rep, Period> timeout) noexcept {
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count();
      return ns >= 0 ? static_cast<uint64_t>(ns) : uint64_t{0};
    }

    raw_type raw_{};
  };

  enum class barrier_wait_result {
    passed,
    serial_thread,
  };

  class barrier final {
  public:
    using raw_type = uv_barrier_t;

    explicit barrier(unsigned int count) {
      throw_if_error(uv_barrier_init(&raw_, count));
    }

    barrier(const barrier&) = delete;
    barrier& operator=(const barrier&) = delete;
    barrier(barrier&&) = delete;
    barrier& operator=(barrier&&) = delete;

    ~barrier() {
      uv_barrier_destroy(&raw_);
    }

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    barrier_wait_result wait() noexcept {
      return uv_barrier_wait(&raw_) == 0
        ? barrier_wait_result::passed
        : barrier_wait_result::serial_thread;
    }

  private:
    raw_type raw_{};
  };

  class thread_key final {
  public:
    using raw_type = uv_key_t;

    thread_key() {
      throw_if_error(uv_key_create(&raw_));
    }

    thread_key(const thread_key&) = delete;
    thread_key& operator=(const thread_key&) = delete;
    thread_key(thread_key&&) = delete;
    thread_key& operator=(thread_key&&) = delete;

    ~thread_key() {
      uv_key_delete(&raw_);
    }

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    void *get() noexcept {
      return uv_key_get(&raw_);
    }

    const void *get() const noexcept {
      return uv_key_get(const_cast<raw_type *>(&raw_));
    }

    template<class T>
    T *get() noexcept {
      return static_cast<T *>(get());
    }

    template<class T>
    const T *get() const noexcept {
      return static_cast<const T *>(get());
    }

    void set(void *value) noexcept {
      uv_key_set(&raw_, value);
    }

    template<class T>
    void set(T *value) noexcept {
      set(static_cast<void *>(value));
    }

    void clear() noexcept {
      set(nullptr);
    }

  private:
    raw_type raw_{};
  };

  class once final {
  public:
    using raw_type = uv_once_t;

    once() = default;

    once(const once&) = delete;
    once& operator=(const once&) = delete;
    once(once&&) = delete;
    once& operator=(once&&) = delete;

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    void run(void (*callback)()) noexcept {
      uv_once(&raw_, callback);
    }

    template<auto Callback>
    void run_static() noexcept {
      uv_once(&raw_, []() noexcept {
        detail::invoke_static_callback<Callback>();
      });
    }

  private:
    raw_type raw_ = UV_ONCE_INIT;
  };

  class thread final {
  public:
    using raw_type = uv_thread_t;

    thread() = default;

    template<class Callback>
      requires std::invocable<std::decay_t<Callback>&>
    explicit thread(Callback &&callback) {
      start(std::forward<Callback>(callback));
    }

    thread(const thread&) = delete;
    thread& operator=(const thread&) = delete;
    thread(thread&&) = delete;
    thread& operator=(thread&&) = delete;

    ~thread() {
      if (joinable_) {
        std::terminate();
      }
    }

    raw_type *native() noexcept { return &raw_; }
    const raw_type *native() const noexcept { return &raw_; }

    bool joinable() const noexcept {
      return joinable_;
    }

    template<class Callback>
      requires std::invocable<std::decay_t<Callback>&>
    void start(Callback &&callback) {
      if (joinable_) {
        throw error(UV_EBUSY);
      }

      using callback_type = std::decay_t<Callback>;
      auto state = std::make_unique<callback_state<callback_type>>(std::forward<Callback>(callback));
      throw_if_error(uv_thread_create(&raw_, &thread::thread_trampoline, state.get()));
      state.release();
      joinable_ = true;
    }

    void join() {
      if (!joinable_) {
        throw error(UV_EINVAL);
      }

      throw_if_error(uv_thread_join(&raw_));
      joinable_ = false;
    }

#if UVPP_HAS_THREAD_DETACH
    void detach() {
      if (!joinable_) {
        throw error(UV_EINVAL);
      }

      throw_if_error(uv_thread_detach(&raw_));
      joinable_ = false;
    }
#endif

    static raw_type self() noexcept {
      return uv_thread_self();
    }

    static bool equal(const raw_type &left, const raw_type &right) noexcept {
      return uv_thread_equal(&left, &right) != 0;
    }

  private:
    struct callback_state_base {
      callback_state_base() = default;
      callback_state_base(const callback_state_base&) = delete;
      callback_state_base& operator=(const callback_state_base&) = delete;
      virtual ~callback_state_base() = default;
      virtual void run() noexcept = 0;
    };

    template<class Callback>
    struct callback_state final : callback_state_base {
      explicit callback_state(Callback cb)
        : callback(std::move(cb)) {}

      void run() noexcept override {
        detail::invoke_callback(callback);
      }

      Callback callback;
    };

    static void thread_trampoline(void *arg) noexcept {
      std::unique_ptr<callback_state_base> state{static_cast<callback_state_base *>(arg)};
      state->run();
    }

    raw_type raw_{};
    bool joinable_ = false;
  };

}
