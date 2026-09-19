# Errors

V3 distinguishes operational failures, C++ setup failures, and invalid API use.
The high-level network awaits report native submission and completion failures as
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

The currently implemented paired owner operation is close:

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
introduce another ownership hierarchy. Full connect/read/write/send counterparts
are still proposed; do not assume `uv::ops::write` or similar names exist.
The target raw layer uses explicit operational results; its namespace and error
policy are not yet migrated throughout the implementation.

## Setup and misuse

Result-oriented operations are not automatically `noexcept`: allocation and
other documented C++ preparation failures can throw. Wrong-loop use currently
raises `std::logic_error` in owner awaits. Active-handle destruction and certain
lifetime violations terminate. Neither is a recoverable native error result.

Cancellation is a request; only actual completion releases borrowed storage.
Completed cooperative cancellation uses `UV_ECANCELED`. An in-flight write/send
can still complete normally after stop. See [coroutines](coroutines.md).

No exception may escape a libuv C callback. The underlying callback invocation
boundary terminates on an escaping user exception; coroutine promises instead
capture exceptions for task observation. Complete unobserved-failure routing and
setup policies remain in [proposal 006](../proposals/006-errors-and-results.md).
