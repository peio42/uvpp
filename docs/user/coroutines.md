# Experimental Coroutines

This is the first, deliberately small v3 coroutine slice. Include its focused
headers; it is not a stable v2 API.

```cpp
#include <chrono>

#include <uvpp/co/sleep.hpp>
#include <uvpp/co/resource_scope.hpp>
#include <uvpp/co/task_scope.hpp>
#include <uvpp/net/tcp_connection.hpp>
#include <uvpp/net/tcp_listener.hpp>

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
  auto value = execution.take_result(); // 42
  loop.close();
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

## Experimental TCP connect

`uv::tcp_connection::connect(ipv4_or_ipv6)` creates a movable owner in the
awaiting task's loop. It is experimental. A connect submission error and an
asynchronous connection error are both thrown at the `co_await`; a failed
connection is closed before delivery.
The native `uv_tcp_t` stays address-stable when the owner moves.

Destroying the owner starts an internal asynchronous close. Keep driving its loop
until it becomes idle, including after an exception unwinds a connected owner.

## Closing experimental owners

`co_await socket.close()` starts native close and resumes only after its close
callback. Repeated close awaits join one native close. `request_close()` starts
the same transition without waiting; keep driving the loop through completion.

Connections and UDP sockets reject close with `UV_EBUSY` while their borrowed I/O
is active. Join or stop that work first. Listeners instead quiesce one active
`accept()`, which resumes with `UV_ECANCELED` after its provisional child has
closed. `close()` does not shut down a stream protocol or join application tasks.

```cpp
co_await workers.join();
co_await socket.close();
```

`uv::ops::close(socket)` awaits the same transition and returns `uv::result`
instead of throwing `UV_EBADF` or `UV_EBUSY`. These APIs remain experimental;
their target contract is in the
[asynchronous ownership proposal](../proposals/002-async-ownership.md).

`co_await socket.write(data)` borrows a `std::string_view`: do not destroy,
reallocate, or modify the characters until the await resumes, even if a stop
request has been made. Once it resumes, the bytes are reusable.
`write_copy()` is intentionally not implemented. The experimental owner permits
one pending write; submission and completion failures throw at the await.

`co_await socket.read_some(std::span<std::byte>)` borrows mutable caller storage
until it returns. Its result exposes `count()` and `eof()`; an operational error
throws at the await. Each call is one-shot: it stops the native reader and releases
both read slots before resuming, so another `read_some` may immediately follow.

Connections are affine to the loop that created them. `write` and `read_some`
reject a task bound to another loop. Destroying a direct connection owner with a
pending read or write is an unsupported contract violation: the experimental
implementation asserts and terminates rather than risking a use-after-free. The
TCP-only `resource_scope` below likewise requires task join before cleanup.

## Experimental TCP listener

`uv::tcp_listener` is the server-side counterpart. Construct it on its loop with
an address; it begins listening immediately. `accept()` is a one-shot await that
returns a distinct movable `tcp_connection` owner. The listener and the accepted
connection have separate stable native storage and separate close completion.

```cpp
uv::co::task<void> handle(uv::tcp_connection connection) {
  std::array<std::byte, 4096> buffer;
  auto read = co_await connection.read_some(buffer);
  // connection begins asynchronous close when this task exits.
}

uv::co::task<void> serve_one(uv::tcp_listener &listener) {
  auto connection = co_await listener.accept();
  co_await handle(std::move(connection));
  co_await listener.close();
}

int main() {
  uv::loop loop;
  uv::tcp_listener listener(loop, uv::ipv4{"127.0.0.1", 8080});
  auto execution = uv::co::spawn(loop, serve_one(listener));
  loop.run();
  execution.rethrow_if_failed();
  loop.close();
}
```

`accept()` is exclusive and affine to the listener's loop; a second pending
`accept()` throws `UV_EBUSY`. `co_await close()` quiesces one active accept, then
waits for native close completion. Destroying a listener with an active accept
remains an unsupported contract violation.

There is deliberately no `serve(handler)` or implicit handler spawning in this
slice. `uv_listen` is a persistent native notification source, while an accept
awaiter consumes exactly one notification (libuv requires `uv_accept()` during
that notification). Consequently a long-running server must keep an accept waiter
armed; this slice has no hidden accepted-connection queue or overflow policy. The
example above is deliberately `serve_one`, not a general sequential server loop.
If libuv notifies the listener while no `accept_awaiter` is armed, this one-shot
slice deliberately ignores that notification: it does not call `uv_accept()` and
does not queue a connection. This is an experimental limitation, not a final
server policy.

The accepted owner and listener can both be transferred into the TCP-only
`resource_scope` below. Independently spawned handlers still require the caller
to keep accepts armed and choose an overload policy.

## Experimental task scopes

`uv::co::task_scope` is a non-movable same-loop owner for immediately started
`task<void>` children. Construct it with the loop, use it from a task bound to that
same loop, then join it exactly once. It retains child frames until every child has
completed; only then does `join()` resume and throw the first child exception, if
any. The first unhandled child exception makes the scope fail fast: it requests
cooperative cancellation for every sibling, joins every child, then rethrows that
first exception. `request_stop()` provides the same request explicitly; it is a
loop-thread operation.

```cpp
uv::co::task<void> handle(uv::tcp_connection_view connection);

uv::co::task<void> serve_two(uv::loop &loop) {
  uv::co::task_scope tasks(loop);
  uv::co::resource_scope resources(loop);
  auto listener = resources.own(
      uv::tcp_listener{loop, uv::ipv4{"127.0.0.1", 8080}});
  for (int count = 0; count != 2; ++count) {
    auto connection = resources.own(co_await listener.accept());
    tasks.spawn(handle(connection.view()));
  }
  co_await tasks.join();
  co_await resources.finish();
}

int main() {
  uv::loop loop;
  auto execution = uv::co::spawn(loop, serve_two(loop));
  loop.run();
  execution.rethrow_if_failed();
  loop.close();
}
```

`resources.own()` transfers the sole connection owner into `resource_scope`; the
handler receives only a `tcp_connection_view`. `task_scope::join()` finishes task
execution, while `resources.finish()` starts internal close and waits for the
actual `uv_close` callback before destroying owner storage. A view retained after
`finish()` diagnoses use instead of accessing released native state. The caller
must join tasks before calling `finish()`; cleanup with active borrowed I/O is
rejected with `std::logic_error`. This rejection is recoverable: keep the scope
alive, settle/join the borrowing tasks, then await a new `resources.finish()` task.
Earlier successful closes are not rolled back; their views are already invalid.
The retry closes only the remaining resources.

`finish()` returns a cold task borrowing the scope. Merely constructing or
abandoning that task does not seal adoption. Starting it on the associated loop
seals `own()` permanently, including after a cleanup failure. A wrong-loop await
throws before changing scope state. Overlapping cleanup attempts throw
`std::logic_error`; after successful cleanup, another await succeeds immediately
on the associated loop. Keep the scope alive through all tasks that borrow it.

Cleanup stops at the first failure and propagates that exception; it does not
aggregate errors or join application tasks. Setup/allocation failures may also
throw, and completed cleanup remains committed. Preserve any primary task failure
separately while handling cleanup errors and retrying. Cancellation does not
shorten native close completion. `resource_scope` destruction with remaining
resources is a terminating contract violation, even after a caught cleanup error;
`task_scope` likewise requires its outstanding work to be settled.

### Cooperative stop

`scope.request_stop()` is a request, not completion. In this slice, a scoped
`sleep_for`, `tcp_connection::read_some`, or `tcp_listener::accept` terminates
with `UV_ECANCELED`; each releases its native callback claim before resuming the
task. An already submitted TCP `write` or `connect` is not physically cancelled:
it completes normally, and `join()` continues to wait for it. A borrowed write
buffer must therefore remain valid and unchanged until that completion even after
a stop request. A task can observe the inherited request with
`co_await uv::co::stop_requested()`.
