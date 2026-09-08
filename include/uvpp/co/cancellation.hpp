#pragma once

#include <cassert>
#include <utility>

namespace uv::co::detail {

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

} // namespace uv::co::detail
