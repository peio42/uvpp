# Experimental Coroutines

This is the first, deliberately small v3 coroutine slice. Include its focused
header; it is not included by `uvpp/uv.hpp` and is not a stable v2 API.

```cpp
#include <chrono>

#include <uvpp/co/sleep.hpp>

using namespace std::chrono_literals;

uv::co::task<void> wait_once() {
  co_await uv::co::sleep_for(50ms);
}

int main() {
  uv::loop loop;
  auto execution = uv::co::spawn(loop, wait_once());

  loop.run();
  execution.rethrow_if_failed();
  loop.close();
}
```

`task<void>` is cold: constructing `wait_once()` does not execute its body or bind
it to a loop. `spawn(loop, task)` consumes it, binds its execution context to that
loop, and starts it. `sleep_for` uses an event-loop timer on that inherited loop;
it does not call the blocking `uv_sleep()`. Positive durations below a millisecond
round up to one millisecond.

Keep the returned `spawn_handle` alive while the task is running, drive the loop
until it completes, then call `rethrow_if_failed()` to observe a task exception.
This first slice deliberately has no task scopes, cancellation, child-task await,
or asynchronous join. Destroying an active `spawn_handle` terminates the process
instead of releasing a coroutine frame that libuv may still reference.
