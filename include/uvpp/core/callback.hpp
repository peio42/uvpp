#pragma once

#include <cstddef>
#include <exception>
#include <utility>

namespace uv::detail {

  [[noreturn]] inline void callback_exception_boundary() noexcept {
    std::terminate();
  }

  template<class Callback, class... Args>
  void invoke_callback(Callback &&callback, Args&&... args) noexcept {
    try {
      std::forward<Callback>(callback)(std::forward<Args>(args)...);
    } catch (...) {
      callback_exception_boundary();
    }
  }

  // A callback slot for callbacks that may be invoked repeatedly. Moving the
  // active callable out keeps it alive when user code replaces the slot from
  // inside that callable. A generation distinguishes an untouched empty slot
  // from a deliberate replacement with an empty callback.
  template<class Callback>
  class persistent_callback_slot {
  public:
    void replace(Callback callback) {
      callback_ = std::move(callback);
      ++generation_;
    }

    template<class Invocation>
    bool invoke(Invocation &&invocation) noexcept {
      const auto generation = generation_;
      auto callback = std::move(callback_);
      callback_ = {};

      if (!callback) {
        return false;
      }

      detail::invoke_callback(std::forward<Invocation>(invocation), callback);

      if (generation_ == generation) {
        callback_ = std::move(callback);
      }
      return true;
    }

  private:
    Callback callback_{};
    std::size_t generation_ = 0;
  };

  template<auto Callback, class... Args>
  void invoke_static_callback(Args&&... args) noexcept {
    try {
      Callback(std::forward<Args>(args)...);
    } catch (...) {
      callback_exception_boundary();
    }
  }

}
