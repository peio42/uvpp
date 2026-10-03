#pragma once

#include <cassert>
#include <utility>

namespace uv::detail {

// Exclusive one-shot callback claim. Native lifecycle, cancellation, and
// retained payloads belong to the containing family, not this slot.
template<class... Args>
class one_shot_callback_slot {
public:
  using callback = void (*)(void *, Args...) noexcept;

  bool claimed() const noexcept { return context_ != nullptr; }
  bool claimed_by(const void *context) const noexcept {
    return claimed() && context_ == context;
  }

  bool claim(void *context, callback cb) noexcept {
    assert(context != nullptr);
    assert(cb != nullptr);
    if (claimed()) {
      return false;
    }
    context_ = context;
    callback_ = cb;
    return true;
  }

  bool release(void *context) noexcept {
    if (!claimed_by(context)) {
      return false;
    }
    context_ = nullptr;
    callback_ = nullptr;
    return true;
  }

  void deliver(Args... args) noexcept {
    auto *context = std::exchange(context_, nullptr);
    auto cb = std::exchange(callback_, nullptr);
    // Delivery may reclaim this slot or destroy its containing state. All
    // accesses to the slot must precede the call.
    if (cb != nullptr) {
      cb(context, std::forward<Args>(args)...);
    }
  }

private:
  void *context_ = nullptr;
  callback callback_ = nullptr;
};

} // namespace uv::detail
