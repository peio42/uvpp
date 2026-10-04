# Errors

V3 distinguishes operational failures, C++ setup failures, and invalid API use.
The throwing high-level network awaits report native submission and completion failures as
`uv::error` at the await expression. Stream EOF remains a normal typed outcome.

```cpp
// Inside a task; include <uvpp/net/tcp_connection.hpp>.
try {
  auto socket = co_await uv::tcp_connection::connect(
      uv::ipv4{"127.0.0.1", 8080});
  co_await socket.close();
} catch (const uv::error& error) {
  // Observe the failure here. Continue driving the loop for pending cleanup.
}
```

A failed connect closes provisional native storage before delivering its failure.
An exception escaping a task is stored by its promise. Awaiting a child rethrows
it; observe a completed root with `take_result()` or `rethrow_if_failed()`.
A task-scope join waits for all children before rethrowing the first child failure.

## Explicit results

`uv::result<T>` holds a value or `uv::error_code`; `uv::status` aliases
`uv::result<void>`. Boolean conversion and `has_value()` indicate success.
`value()` throws `uv::error` for an error result; `error()` returns an empty code
on success. Values can be move-only. `error_code::native()`, `name()`, and
`message()` expose libuv diagnostics; conversion to `std::error_code` is explicit.

Network owners also provide synchronous creation pairs:

| Throwing construction | Explicit-result factory | Result |
| --- | --- | --- |
| `uv::tcp_listener{loop, address, backlog}` | `uv::ops::make_tcp_listener(loop, address, backlog)` | `result<tcp_listener>` |
| `uv::pipe_listener{loop, name, ipc, backlog}` | `uv::ops::make_pipe_listener(loop, name, ipc, backlog)` | `result<pipe_listener>` |
| `uv::udp_socket{loop, address}` | `uv::ops::make_udp_socket(loop, address)` | `result<udp_socket>` |

TCP and UDP accept IPv4 and IPv6. Listener backlog defaults to 64; pipe IPC
mode defaults to false. These factories return the same move-only owners:

```cpp
auto created = uv::ops::make_tcp_listener(loop, uv::ipv4{"127.0.0.1", 8080});
if (created) {
  auto listener = std::move(created).value();
  // Use listener on loop; close or transfer it into a resource scope.
} else {
  auto code = created.error();
  // Continue driving loop for any pending native cleanup.
}
```

Native init/bind/listen failures throw `uv::error` in constructors and become
error results in factories. If initialization succeeded before failure, both
schedule native close before delivering the error and retain storage through its
callback. Error delivery does not await close or run the loop: keep the loop alive
and drive it before closing it, even when no owner was returned. C++ storage and
pipe-name preparation failures still throw, including `std::bad_alloc`; factories
are not `noexcept`. Address-value construction happens before the factory call
and retains its own error contract.

Network owners provide paired I/O and close operations:

```cpp
// Inside a task, with a high-level socket owner.
auto result = co_await uv::ops::close(socket);
if (!result) {
  auto code = result.error();
  // UV_EBUSY means active borrowed I/O must settle before retrying close.
  (void)code;
}
```

`uv::ops` selects explicit operational results on the same owners. It does not
introduce another ownership hierarchy. DNS also provides paired request surfaces:
`co_await uv::resolve(...)` throws operational failure and
`co_await uv::ops::resolve(...)` returns
`uv::result<uv::resolved_addresses>`. The network I/O counterparts are also available:

| Throwing member await | Explicit-result await | Await result |
| --- | --- | --- |
| `connection.read_some(buffer)` | `uv::ops::read_some(connection, buffer)` | `result<Connection::read_some_result>` (count and EOF) |
| `connection.write(bytes)` | `uv::ops::write(connection, bytes)` | `status` |
| `listener.accept()` | `uv::ops::accept(listener)` | `result<tcp_connection>` or `result<pipe_connection>` |
| `socket.recv_from(buffer)` | `uv::ops::recv_from(socket, buffer)` | `result<udp_socket::recv_from_result>` |
| `socket.send_to(bytes, address)` | `uv::ops::send_to(socket, bytes, address)` | `status` |

Stream operations cover TCP and pipes; read/write and UDP operations also accept
borrowed resource-scope views. Explicit results cover submission and completion
errors, including busy, closed, and completed cancellation outcomes. EOF is a
successful stream-read outcome. Both policies use the same native operations,
callback slots, borrowed buffers, cancellation, and cleanup. Accept failures are
delivered only after provisional native storage has closed. No additional
operation allocation is introduced by the result facade.

`write` takes `std::string_view`; `send_to` mirrors the member overloads (byte
spans for IPv4/IPv6 and a string view for IPv4). Buffer-length validation can
still throw `std::length_error` during preparation. Wrong-loop use and expired
views remain `std::logic_error`, and accept storage allocation can throw.
Connect and IPC handle-passing counterparts remain proposed.

Filesystem provides the first complete owner-I/O pair: `uv::fs::open/read/write/close`
throw native operational failures at the await expression, while
`uv::ops::fs::open/read/write/close` return `result<file>`,
`result<file_read_result>`, `result<size_t>`, and `status`. See
[Filesystem](filesystem.md) for borrowed-buffer and terminal-close rules.
`signal_source` also pairs `co_await source.next()`, which throws an operational
failure, with `co_await uv::ops::next(source)`, which returns
`result<signal_number>`. A completed wait cancellation is `UV_ECANCELED`; a
second concurrent waiter is `UV_EBUSY`. See [Signals](signals.md).

The target raw layer uses explicit operational results; its namespace and error
policy are not yet migrated throughout the implementation.

## Setup and misuse

Result-oriented operations are not automatically `noexcept`: allocation and
other documented C++ preparation failures can throw. Wrong-loop use currently
raises `std::logic_error` in owner awaits. Certain lifetime violations terminate.
Destroying an active spawn handle requests stop and retains its execution until
actual completion. A root failure left unobserved when its public handle is lost
is routed exactly once to the loop’s unobserved-failure handler; the default
policy terminates. Neither is a recoverable native error result.

Cancellation is a request; only actual completion releases borrowed storage.
Completed cooperative cancellation uses `UV_ECANCELED`. An in-flight write/send
can still complete normally after stop. See [coroutines](coroutines.md).

No exception may escape a libuv C callback. The underlying callback invocation
boundary terminates on an escaping user exception; coroutine promises instead
capture exceptions for task observation. Setup policies remain in
[proposal 006](../proposals/006-errors-and-results.md).

`fs_event_source` pairs throwing `next()` with
`uv::ops::next(source) -> result<fs_event>`. Native callback errors, cancellation,
busy, and closed-owner failures follow that pairing. C++ filename materialization
failures are captured inside the native callback and rethrown at either await
expression. See [Filesystem notifications](fs-events.md).
