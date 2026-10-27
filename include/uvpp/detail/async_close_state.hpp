#pragma once

#include <cassert>
#include <coroutine>
#include <utility>
#include <vector>

namespace uv::detail {

// Private bookkeeping shared by high-level handle owners. It deliberately does
// not know a native handle, quiescence protocol, or how uv_close() is started.
// The owning family calls begin(), performs its own pre-close work, and submits
// uv_close(); its close callback calls complete().
class async_close_state {
public:
  enum class phase {
    open,
    closing,
    closed,
  };

  bool closing() const noexcept { return phase_ != phase::open; }
  bool closed() const noexcept { return phase_ == phase::closed; }
  bool open() const noexcept { return phase_ == phase::open; }

  // Claims the close transition exactly once. Native close initiation remains
  // the responsibility of the resource family immediately after this returns.
  bool begin() noexcept {
    if (phase_ != phase::open) {
      return false;
    }
    phase_ = phase::closing;
    return true;
  }

  // May allocate at coroutine suspension. It never starts native close: this
  // permits a family to quiesce its own callback sources before uv_close().
  bool add_waiter(std::coroutine_handle<> continuation) {
    assert(phase_ != phase::closed);
    if (phase_ == phase::closed) {
      return false;
    }
    waiters_.push_back(continuation);
    return true;
  }

  // Connect/accept failure paths call this from native callbacks, where they
  // need a fixed no-allocation continuation slot instead of vector growth.
  void set_callback_waiter(std::coroutine_handle<> continuation) noexcept {
    assert(!callback_waiter_);
    callback_waiter_ = continuation;
  }

  void owner_released() noexcept {
    assert(!owner_released_);
    owner_released_ = true;
  }

  bool can_destroy_now() const noexcept {
    return owner_released_ && phase_ == phase::closed && !completion_active_;
  }

  // Terminal callback protocol: detach all frame-facing handles before the
  // first resumption, then invoke the supplied deletion action only after
  // delivery. The action must not return to use the owning state.
  template<class ResumeDelete>
  void complete(ResumeDelete &&delete_state) noexcept {
    phase_ = phase::closed;
    completion_active_ = true;
    auto callback_waiter = std::exchange(callback_waiter_, {});
    auto waiters = std::move(waiters_);
    if (callback_waiter) {
      callback_waiter.resume();
    }
    for (auto continuation : waiters) {
      if (continuation) {
        continuation.resume();
      }
    }
    completion_active_ = false;
    if (owner_released_) {
      std::forward<ResumeDelete>(delete_state)();
    }
  }

private:
  phase phase_ = phase::open;
  std::vector<std::coroutine_handle<>> waiters_{};
  std::coroutine_handle<> callback_waiter_{};
  bool owner_released_ = false;
  bool completion_active_ = false;
};

} // namespace uv::detail
