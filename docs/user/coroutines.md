# Coroutines

The v3 coroutine API is implemented experimentally. Use focused headers;
its execution and lifetime contracts below describe the current implementation.

```cpp
#include <chrono>

#include <uvpp/co/sleep.hpp>

using namespace std::chrono_literals;

uv::co::task<void> wait_once() {
  co_await uv::co::sleep_for(50ms);
}

uv::co::task<int> answer() {
  co_await uv::co::sleep_for(10ms);
  co_return 42;
}

uv::co::task<void> parent() {
  auto value = co_await answer(); // value == 42
}

int main() {
  uv::loop loop;
  auto execution = uv::co::spawn(loop, answer());

  loop.run();
  loop.close();
  auto value = execution.take_result(); // 42
}
```

`task<T>` is cold: constructing `wait_once()` does not execute its body or bind
it to a loop. `spawn(loop, task<T>)` consumes it, binds its execution context to
that loop, and starts it, returning a move-only `spawn_handle<T>`. `sleep_for`
uses an event-loop timer on that inherited loop; it does not call the blocking
`uv_sleep()`. Positive durations below a millisecond round up to one millisecond.

Awaiting a temporary child task consumes it, binds it to its parent loop, and
delivers its value by move. A child exception is thrown at the parent `co_await`;
an uncaught one is observable through the root `spawn_handle`.

Keep the returned `spawn_handle<T>` alive while the task is running, and drive
the loop until it completes. `co_await execution.join()` is a same-loop completion
barrier and may be used by multiple tasks. It does not consume the result. For a
non-void root, `take_result()` moves the value exactly once; it rethrows a stored
task exception. `has_result()` reports whether that value remains available.
For a void root, use `rethrow_if_failed()` after completion.

`request_stop()` requests cooperative cancellation for the root and nested child
tasks; it is loop-thread-only and does not imply completion. Destroying an active
`spawn_handle<T>` terminates the process instead of releasing a coroutine frame
that libuv may still reference. There is no implicit detach.

## Experimental task scopes

`uv::co::task_scope` is a non-movable same-loop owner for immediately started
`task<void>` children. Construct it with the loop, use it from a task bound to that
same loop, then join it exactly once. It retains child frames until every child has
completed; only then does `join()` resume and throw the first child exception, if
any. The first unhandled child exception makes the scope fail fast: it requests
cooperative cancellation for every sibling, joins every child, then rethrows that
first exception. `request_stop()` provides the same request explicitly; it is a
loop-thread operation.

Include `<uvpp/co/task_scope.hpp>`. Join child work on both success and failure
paths before destroying the scope or borrowed data. `join()` observes child
failures only after all children have settled. It does not close resource owners;
compose it with a [resource scope](ownership-and-lifetime.md#resource-scopes).

For example, this task starts two timer children and waits for both:

```cpp
#include <exception>
#include <uvpp/co/sleep.hpp>
#include <uvpp/co/task_scope.hpp>

uv::co::task<void> pause_once() {
  co_await uv::co::sleep_for(std::chrono::milliseconds{10});
}

uv::co::task<void> run_children(uv::loop& loop) {
  uv::co::task_scope tasks(loop);
  // Capture setup failure so already-started children can still be joined.
  std::exception_ptr setup_error;
  try {
    tasks.spawn(pause_once());
    tasks.spawn(pause_once());
  } catch (...) {
    setup_error = std::current_exception();
    tasks.request_stop();
  }
  try {
    co_await tasks.join();
  } catch (...) {
    if (!setup_error) throw;
  }
  if (setup_error) std::rethrow_exception(setup_error);
}
```

Spawn `run_children(loop)` on that same loop and observe its root result. If either child fails, `join()` first
settles both children, then throws. The setup-error path above preserves its
original failure after joining any child that did start.

### Cooperative stop

`scope.request_stop()` is a request, not completion. In this slice, a scoped
`sleep_for`, `tcp_connection::read_some`, or `tcp_listener::accept` terminates
with `UV_ECANCELED`; each releases its native callback claim before resuming the
task. An already submitted TCP `write` or `connect` is not physically cancelled:
it completes normally, and `join()` continues to wait for it. A borrowed write
buffer must therefore remain valid and unchanged until that completion even after
a stop request. A task can observe the inherited request with
`co_await uv::co::stop_requested()`.

Task frames and spawn bookkeeping may allocate. Continuations can resume from
native completion on the same loop thread; there is no implicit worker-thread
hop or general posting queue. Public detach and automatic retention after active
spawn-handle destruction remain unimplemented; do not rely on the target fallback
in proposals as an available behavior.

See [networking](networking/tcp.md) for owner operations and
[errors](errors.md) for exception observation.
