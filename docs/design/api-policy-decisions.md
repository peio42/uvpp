# API Policy Decisions

This file records small but important API policy decisions that should stay
consistent as uvpp grows. It is intentionally more concrete than the general
API principles.

## `try_*` Names

Reserve `try_*` for non-throwing variants of APIs that would otherwise throw on
immediate libuv failure.

`try_*` functions should return `std::error_code` or a similarly explicit
non-throwing status channel, and should not throw for normal immediate failure.

Example:

```cpp
std::error_code close_error = loop.try_close();
```

Do not use `try_*` just because the underlying libuv function contains `try` in
its name.

## Immediate Non-Blocking Operations

When libuv exposes a synchronous "try now" operation, name the uvpp wrapper
`*_now()`.

Examples:

```cpp
auto sent = udp.send_now(buffer, address);
auto written = stream.write_now(buffer);
```

These functions do not own asynchronous state, do not take request objects, and
do not take callbacks.

If a non-blocking "nothing happened" status is part of the normal control flow
for that operation, model it explicitly in a typed result object instead of
throwing. For example, an operation based on `uv_try_write()` should expose a
`would_block()` branch for `UV_EAGAIN`.

## Typed Result Objects

Use a typed result object when a libuv return value contains meaningful payload
and status in the same integer.

Examples:

- stream reads: positive values are byte counts, `UV_EOF` is a normal EOF branch;
- synchronous non-blocking writes: positive values are byte counts, `UV_EAGAIN`
  means the operation would block.

Typed result objects should make ordinary branches visible:

```cpp
if (result.would_block()) {
  return;
}

if (result.has_error()) {
  auto ec = result.error_code();
  return;
}

auto bytes = result.bytes_written();
```

Expose raw status for interop when useful, but avoid forcing callers to decode
libuv sentinel values for common control flow.

## Borrowed Views

Borrowed views must be produced by named functions, not implicit conversion
operators.

Examples:

```cpp
uv::handle_view handle = timer.view();
uv::buffer_view buffer = storage.view();
```

The `.view()` spelling makes the ownership relationship visible: the returned
object does not own the underlying native handle, bytes, or storage.

Do not add implicit conversions from owning or address-stable wrappers to
borrowed views such as `handle_view`.

## Native Access

Raw libuv access stays explicit through named helpers:

```cpp
uv_tcp_t* raw = tcp.native();
uv_stream_t* stream = tcp.native_stream();
uv_handle_t* handle = tcp.native_handle();
```

Do not add implicit conversion operators to raw libuv pointers.

## Chrono For Durations

Use `std::chrono` for durations in public APIs.

If libuv represents a timeout with a sentinel value, model the sentinel in the
type instead of leaking it directly when practical. For example,
`loop.backend_timeout()` returns `std::optional<std::chrono::milliseconds>`,
where `std::nullopt` represents an infinite wait.

Counters remain integer counts. Do not wrap event counts or loop iteration
counts in duration types.

## Async Lifetime Visibility

If an operation outlives the initiating call, the owner of the operation state
must be clear in the function signature.

Low-level request-shaped API:

```cpp
uv::write_request request;
stream.write(request, buffer, callback);
```

Synchronous immediate API:

```cpp
auto result = stream.write_now(buffer);
```

Do not hide request lifetime behind a low-level convenience that accepts only
data and a callback unless the API name and documentation make ownership
explicit.
