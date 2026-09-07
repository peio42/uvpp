# Experimental Coroutines

This is the first, deliberately small v3 coroutine slice. Include its focused
header; it is not included by `uvpp/uv.hpp` and is not a stable v2 API.

```cpp
#include <chrono>

#include <uvpp/co/sleep.hpp>
#include <uvpp/net/tcp_connection.hpp>

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
  auto execution = uv::co::spawn(loop, wait_once());

  loop.run();
  execution.rethrow_if_failed();
  loop.close();
}
```

`task<T>` is cold: constructing `wait_once()` does not execute its body or bind
it to a loop. The experimental root entry point currently accepts `task<void>`:
`spawn(loop, task<void>)` consumes it, binds its execution context to that loop,
and starts it. `sleep_for` uses an event-loop timer on that inherited loop; it does
not call the blocking `uv_sleep()`. Positive durations below a millisecond round
up to one millisecond.

Awaiting a temporary child task consumes it, binds it to its parent loop, and
delivers its value by move. A child exception is thrown at the parent `co_await`;
an uncaught one is observable through the root `spawn_handle`.

Keep the returned `spawn_handle` alive while the task is running, drive the loop
until it completes, then call `rethrow_if_failed()` to observe a task exception.
This first slice deliberately has no task scopes, cancellation, or asynchronous
join. Destroying an active `spawn_handle` terminates the process
instead of releasing a coroutine frame that libuv may still reference.

## Experimental TCP connect

`uv::tcp_connection::connect(ipv4_or_ipv6)` creates a movable owner in the
awaiting task's loop. It is a focused experimental header, not yet an umbrella
`uvpp/uv.hpp` API. A connect submission error and an asynchronous connection error
are both thrown at the `co_await`; a failed connection is closed before delivery.
The native `uv_tcp_t` stays address-stable when the owner moves.

Destroying the owner starts an internal asynchronous close. Keep driving its loop
until it becomes idle, including after an exception unwinds a connected owner.
There is not yet a public `co_await socket.close()` or read API.

`co_await socket.write(data)` borrows a `std::string_view`: do not destroy,
reallocate, or modify the characters until the await resumes, even if a future
cancellation request has been made. Once it resumes, the bytes are reusable.
`write_copy()` is intentionally not implemented. The experimental owner permits
one pending write; submission and completion failures throw at the await.
