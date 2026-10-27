#pragma once

#include <coroutine>

namespace uv::detail {

// Construct the original non-movable awaiter in place; adapt only delivery.
// Native callbacks, cancellation, slots and lifetime use that same operation.
template<class Awaiter>
class network_result_awaiter {
public:
  template<class Factory>
  explicit network_result_awaiter(Factory factory) : operation_{factory()} {}

  bool await_ready() const noexcept { return operation_.await_ready(); }

  template<class Promise>
    requires requires(Awaiter &operation, std::coroutine_handle<Promise> continuation) {
      operation.await_suspend(continuation);
    }
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    return operation_.await_suspend(continuation);
  }

  auto await_resume() { return operation_.await_resume_result(); }

private:
  Awaiter operation_;
};

} // namespace uv::detail
