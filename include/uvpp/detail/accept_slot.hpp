#pragma once

#include <cassert>
#include <utility>

namespace uv::detail {

// The one-shot high-level accept claim associated with a persistent
// uv_listen() source. This is deliberately only a slot protocol: the listener
// family remains responsible for constructing, accepting into, and cleaning up
// its provisional child.
class accept_slot {
public:
  using deliver_function = void (*)(void *, int) noexcept;
  using cancel_function = void (*)(void *) noexcept;

  struct released_slot {
    void *active = nullptr;
    deliver_function deliver = nullptr;
    cancel_function cancel = nullptr;
  };

  bool claimed() const noexcept { return active_ != nullptr; }
  bool claimed_by(const void *active) const noexcept { return active_ == active; }

  void claim(void *active, deliver_function deliver, cancel_function cancel) noexcept {
    assert(active != nullptr);
    assert(deliver != nullptr);
    assert(cancel != nullptr);
    assert(!claimed());
    active_ = active;
    deliver_ = deliver;
    cancel_ = cancel;
  }

  // Detaches every callback-frame pointer before a family invokes either
  // function. The returned record is self-contained, so delivery can resume a
  // coroutine that destroys the listener without a later slot dereference.
  released_slot release() noexcept {
    released_slot released{
        std::exchange(active_, nullptr),
        std::exchange(deliver_, nullptr),
        std::exchange(cancel_, nullptr)};
    if (released.active == nullptr) {
      assert(released.deliver == nullptr);
      assert(released.cancel == nullptr);
    } else {
      assert(released.deliver != nullptr);
      assert(released.cancel != nullptr);
    }
    return released;
  }

  // Native connection notification: release the exclusive listener claim
  // before the family-specific callback handles uv_accept() and user delivery.
  void deliver(int status) noexcept {
    auto released = release();
    if (released.deliver != nullptr) {
      released.deliver(released.active, status);
    }
  }

  // Scope cleanup/cancellation: release the claim before the awaiter begins
  // provisional-child cleanup and eventually resumes its task.
  void quiesce() noexcept {
    auto released = release();
    if (released.cancel != nullptr) {
      released.cancel(released.active);
    }
  }

private:
  void *active_ = nullptr;
  deliver_function deliver_ = nullptr;
  cancel_function cancel_ = nullptr;
};

} // namespace uv::detail
