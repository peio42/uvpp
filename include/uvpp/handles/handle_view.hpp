#pragma once

#include <concepts>

#include <uv.h>

namespace uv {

namespace detail {

template<class T>
concept native_handle_recoverable =
  requires { typename T::raw_type; } &&
  requires(typename T::raw_type *raw, const typename T::raw_type *const_raw) {
    { T::from_native(raw) } -> std::same_as<T&>;
    { T::from_native(const_raw) } -> std::same_as<const T&>;
  };

} // namespace detail

// Non-owning view of any uv_handle_t*, including handles not created by uvpp.
// Provides safe access to common handle operations.
class handle_view {
public:
  explicit handle_view(uv_handle_t *raw) noexcept : raw_{raw} {}

  uv_handle_t *native_handle() noexcept { return raw_; }
  const uv_handle_t *native_handle() const noexcept { return raw_; }

  uv_handle_type type() const noexcept { return raw_->type; }

  // Returns the raw loop pointer; construct a loop_view from it if needed.
  uv_loop_t *native_loop() const noexcept { return raw_->loop; }

  bool active()  const noexcept { return uv_is_active(raw_) != 0; }
  bool closing() const noexcept { return uv_is_closing(raw_) != 0; }
  bool has_ref() const noexcept { return uv_has_ref(raw_) != 0; }

  void ref()   noexcept { uv_ref(raw_); }
  void unref() noexcept { uv_unref(raw_); }
  void close() noexcept { uv_close(raw_, nullptr); }

  // Recovers the uvpp object from the handle pointer.
  // UNSAFE: only valid if this handle was created by uvpp as a T.
  template<class T>
    requires detail::native_handle_recoverable<T>
  T &as() noexcept {
    return T::from_native(reinterpret_cast<typename T::raw_type *>(raw_));
  }

  template<class T>
    requires detail::native_handle_recoverable<T>
  const T &as() const noexcept {
    return T::from_native(reinterpret_cast<const typename T::raw_type *>(raw_));
  }

private:
  uv_handle_t *raw_;
};

} // namespace uv
