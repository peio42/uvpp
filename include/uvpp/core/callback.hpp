#pragma once

#include <exception>
#include <utility>

namespace uvpp::detail {

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

  template<auto Callback, class... Args>
  void invoke_static_callback(Args&&... args) noexcept {
    try {
      Callback(std::forward<Args>(args)...);
    } catch (...) {
      callback_exception_boundary();
    }
  }

}
