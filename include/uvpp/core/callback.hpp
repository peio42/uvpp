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
    std::size_t generation() const noexcept { return generation_; }

    void replace(Callback callback) {
      callback_ = std::move(callback);
      ++generation_;
    }

    Callback take() noexcept {
      auto callback = std::move(callback_);
      callback_ = {};
      ++generation_;
      return callback;
    }

    // Keep one native buffer loan paired with its subscription across the
    // allocator/read callback boundary. A retired delivery cannot restore a slot.
    struct delivery {
      Callback callback{};
      std::size_t generation = 0;
    };

    delivery checkout() noexcept {
      delivery out{std::move(callback_), generation_};
      callback_ = {};
      return out;
    }

    void restore(delivery pending) noexcept {
      if (generation_ == pending.generation)
        callback_ = std::move(pending.callback);
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
