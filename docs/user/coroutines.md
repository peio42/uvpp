# Experimental Coroutines

This is the first, deliberately small v3 coroutine slice. Include its focused
header; it is not included by `uvpp/uv.hpp` and is not a stable v2 API.

```cpp
#include <chrono>

#include <uvpp/co/sleep.hpp>
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
The root `spawn_handle` still has no cancellation or asynchronous join;
`task_scope` below supplies structured joining and cooperative stop for children.
Destroying an active `spawn_handle` terminates the process
instead of releasing a coroutine frame that libuv may still reference.

## Experimental TCP connect

`uv::tcp_connection::connect(ipv4_or_ipv6)` creates a movable owner in the
awaiting task's loop. It is a focused experimental header, not yet an umbrella
`uvpp/uv.hpp` API. A connect submission error and an asynchronous connection error
are both thrown at the `co_await`; a failed connection is closed before delivery.
The native `uv_tcp_t` stays address-stable when the owner moves.

Destroying the owner starts an internal asynchronous close. Keep driving its loop
until it becomes idle, including after an exception unwinds a connected owner.
There is not yet a public `co_await socket.close()` API.

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
reject a task bound to another loop. Until a resource scope can retain native
operation state through asynchronous cleanup,
destroying a connection with a pending read or write is an unsupported contract
violation: the experimental implementation asserts and terminates rather than
risking a use-after-free.

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
  listener.close(); // Close before the root task completes.
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
`accept()` throws `UV_EBUSY`. Destroying or calling `close()` on a listener with
an active accept is, like pending connection I/O, an unsupported contract
violation. `close()` only initiates native close, so the loop must continue to be
driven through close completion.

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

The accepted owner belongs to the receiving task. Passing it by value to a child
task and `co_await`ing that child makes the handler frame own it until completion,
but leaves no accept waiter while the handler is running. Independently spawning a
handler previously had no safe automatic owner/join relationship. The experimental
`task_scope` below provides that task ownership; a future `resource_scope` still
has to retain and close resources during cancellation or failure, and choose the
accepted-connection queue and overload policy.

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
uv::co::task<void> handle(uv::tcp_connection connection);

uv::co::task<void> serve_two(
    uv::tcp_listener &listener, uv::co::task_scope &scope) {
  for (int count = 0; count != 2; ++count) {
    auto connection = co_await listener.accept();
    scope.spawn(handle(std::move(connection)));
  }
  co_await scope.join();
  listener.close();
}

int main() {
  uv::loop loop;
  uv::tcp_listener listener(loop, uv::ipv4{"127.0.0.1", 8080});
  uv::co::task_scope scope(loop);
  auto execution = uv::co::spawn(loop, serve_two(listener, scope));
  loop.run();
  execution.rethrow_if_failed();
  loop.close();
}
```

Passing the connection by value makes the handler task own it. On normal handler
exit, its owner starts asynchronous close; keep driving the loop through that close
completion. `task_scope::join()` joins handler task completion, not yet every
resource close completion. `task_scope` owns executions; a future, distinct
`resource_scope` will own asynchronous owner cleanup and compose with it rather
than merging the two responsibilities. Until that scope exists, a `task_scope`
must not be destroyed without `co_await join()`; doing so terminates rather than
freeing frames that native operations may still reference.

### Cooperative stop

`scope.request_stop()` is a request, not completion. In this slice, a scoped
`sleep_for`, `tcp_connection::read_some`, or `tcp_listener::accept` terminates
with `UV_ECANCELED`; each releases its native callback claim before resuming the
task. An already submitted TCP `write` or `connect` is not physically cancelled:
it completes normally, and `join()` continues to wait for it. A borrowed write
buffer must therefore remain valid and unchanged until that completion even after
a stop request. A task can observe the inherited request with
`co_await uv::co::stop_requested()`.
