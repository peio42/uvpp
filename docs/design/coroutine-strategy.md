# Coroutine Strategy Draft

Status: draft.

This document records the target coroutine direction for uvpp. It is not a
commitment to implement coroutines before the remaining asynchronous wrapper
surface is stable. If later work on DNS, work queue, random, streams, UDP,
watchers, or cancellation contradicts these assumptions, update this document
before implementing the public coroutine API.

## Goals

uvpp coroutine support should layer on top of the explicit request, handle, and
lifetime model. It should not replace the low-level callback API and should not
hide libuv's ownership rules in destructors or implicit conversions.

The coroutine API should make ordinary asynchronous code concise:

```cpp
uv::task<void> read_file(uv::loop &loop, std::string path) {
  auto file = co_await uv::fs::open_file(loop, path, O_RDONLY, 0);
  auto chunk = co_await uv::fs::read_bytes(loop, file, 4096, 0);

  use(chunk.bytes());

  co_await uv::fs::close_file(loop, file);
}
```

It should also preserve uvpp's explicit result style when the caller wants to
handle asynchronous failures without exceptions:

```cpp
uv::task<void> read_file_explicit(uv::loop &loop, std::string path) {
  auto opened = co_await uv::fs::async_open(loop, path, O_RDONLY, 0);
  if (!opened) {
    handle(opened.error_code());
    co_return;
  }

  auto read = co_await uv::fs::async_read(loop, opened.file(), 4096, 0);
  if (!read) {
    handle(read.error_code());
    co_return;
  }

  use(read.bytes());
}
```

## Language Baseline

Coroutine support should be implementable in C++20. Do not raise uvpp's language
baseline to C++23 only to add coroutines.

C++20 provides the coroutine language machinery. uvpp still needs its own
library types such as `task<T>`, operation awaitables, and later possibly
`async_generator<T>` or `channel<T>`.

C++23 library features may be useful internally or under feature-test macros,
but they should not be required for the first public coroutine API:

- `std::expected` could improve future non-throwing value APIs;
- `std::move_only_function` could simplify some callback storage;
- `std::scope_exit` could simplify cleanup helpers;
- `std::generator` is synchronous and does not solve libuv event streams.

## Build Order

Do not design the final coroutine API from filesystem operations alone.

Before stabilizing public coroutine support, implement or at least prototype the
main missing one-shot asynchronous families:

- `getaddrinfo`;
- `getnameinfo`;
- `queue_work`;
- asynchronous `random`;
- request-based stream and UDP operations such as connect, write, shutdown, and
  send.

Filesystem is a good prototype surface because it already has high-level
operation-owned state. It is not enough to validate the whole coroutine design.
DNS, work queue, random, and networking exercise different result shapes,
buffer ownership rules, and cancellation limitations.

## Required Building Blocks

The target coroutine API has three layers.

1. Operation awaitables

   These adapt a single libuv operation to `co_await`. They own any request,
   callback state, copied path strings, owned buffers, and result storage needed
   until completion.

2. `uv::task<T>`

   This is uvpp's coroutine return type for user-written coroutine functions.
   It is needed for a complete and composable API, but it does not need to be
   the first prototype if only operation awaitables are being explored.

3. Stream abstractions

   Repeated event sources should not be forced into the same shape as one-shot
   operations. They need either one-shot adapters such as `async_read_some()`,
   or later async sequence abstractions such as `async_generator<T>` or
   `channel<T>`.

## `uv::task<T>`

`uv::task<T>` is expected to become necessary for the final coroutine story.
Without it, uvpp can expose operations that are awaitable, but cannot provide a
complete way to write, return, spawn, and compose uvpp coroutine functions.

The final design should answer these questions explicitly:

- whether tasks are lazy or eager;
- how `task<void>` is represented;
- how returned values and exceptions are stored;
- how `final_suspend` resumes a continuation;
- what happens when a task object is destroyed before completion;
- how detached tasks are started;
- whether `spawn(loop, task)` is part of the public API;
- how cancellation is requested or reported;
- whether a task is bound to a `loop` or only to the operations it awaits.

The preferred default is a small, explicit C++20 `task<T>` that stores either a
value or an exception and resumes its awaiting continuation at `final_suspend`.
Detached execution should be explicit, for example through `uv::spawn(...)` or a
separate detached task type.

Do not make hidden background tasks from ordinary function calls. Starting work
should remain visible through `co_await`, `spawn`, or a named asynchronous
operation.

## Error Policy

The coroutine API should support both explicit-result and throwing styles.

The low-level coroutine adapters should mirror existing callback result objects:

```cpp
auto opened = co_await uv::fs::async_open(loop, path, O_RDONLY, 0);
auto sent = co_await socket.async_send(bytes, peer);
```

These return typed result objects such as `fs::open_result`,
`fs::read_result`, or `uv::result`. They should not throw for normal
asynchronous completion failure. Immediate submission failure may still throw,
matching the existing low-level policy, unless a specific non-throwing adapter
is added.

Ergonomic high-level coroutine helpers may throw `uv::error` for both immediate
submission failure and asynchronous completion failure:

```cpp
auto file = co_await uv::fs::open_file(loop, path, O_RDONLY, 0);
auto bytes = co_await uv::fs::read_bytes(loop, file, 4096, 0);
co_await stream.write_all(bytes);
```

This dual surface keeps uvpp's explicit result model available while allowing
coroutine code to stay linear where exceptions are acceptable.

Do not encode EOF as a generic exception. EOF is normal stream control flow and
must remain visible in stream read result types.

## One-Shot Awaitables

A one-shot awaitable represents one asynchronous operation that produces one
completion result.

Good candidates:

- filesystem open, close, read, write, stat, scandir, and similar operations;
- DNS `getaddrinfo` and `getnameinfo`;
- asynchronous random;
- work queue operations;
- TCP and pipe connect;
- stream write and shutdown;
- UDP send;
- timer sleep;
- handle close, if lifetime rules are explicit.

Expected usage:

```cpp
auto resolved = co_await uv::net::async_getaddrinfo(loop, node, service);
auto status = co_await client.async_connect(resolved.front());
auto written = co_await client.async_write(payload);
co_await uv::sleep_for(loop, 250ms);
```

Each one-shot awaitable must own or borrow state honestly:

- owned requests stay alive until completion;
- copied path strings stay alive until libuv no longer needs them;
- copied write buffers stay alive until completion in high-level helpers;
- borrowed buffer variants must make the borrow visible in the function name or
  signature;
- handle references remain borrowed and the handle owner must keep them alive.

The awaitable must not use `uv_handle_t::data` or `uv_req_t::data` for uvpp
internals. Those fields remain application-owned.

## Streams, Generators, And Channels

Not every libuv callback family is a one-shot operation. Some APIs represent
event streams.

Examples:

- `stream.read_start`;
- `udp.receive_start`;
- `stream.listen`;
- `fs_event.start`;
- `fs_poll.start`;
- `signal.start`;
- repeating timers.

These should not be forced into `co_await operation()` if the operation is
naturally repeated.

### One-Shot Stream Adapters

For convenience, uvpp may expose one-shot adapters for event sources:

```cpp
auto read = co_await client.async_read_some(4096);
auto accepted = co_await server.async_accept();
auto received = co_await socket.async_receive_some(4096);
```

These are useful and easy to teach, but they must define whether they start and
stop the underlying libuv read or receive watcher for each await, or whether
they rely on a persistent internal watcher. The first version should favor the
simpler and more explicit model unless performance or libuv constraints require
otherwise.

### Async Generators

For repeated values, a future `uv::async_generator<T>` style API may be more
natural:

```cpp
auto reads = client.read_chunks(4096);

while (auto read = co_await reads.next()) {
  if (read->eof()) {
    break;
  }

  use(read->bytes());
}
```

`co_yield` would be an implementation technique for uvpp's generator type, not
necessarily syntax exposed directly to every user. C++23 `std::generator` is
not enough here because these events are asynchronous.

### Channels

A channel is an asynchronous queue between libuv callbacks and coroutine
consumers.

Possible shape:

```cpp
auto reads = client.read_channel(4096);

while (auto read = co_await reads.pop()) {
  use(read->bytes());
}
```

Channels can model buffering, backpressure, closure, and producer-consumer
decoupling. They are more complex than one-shot awaitables and should not be
introduced until the required semantics are clear:

- capacity;
- behavior when full;
- single-consumer or multi-consumer use;
- close and cancellation behavior;
- error propagation;
- interaction with `read_stop()` or watcher shutdown.

## Higher-Level Read API

The low-level stream read API currently exposes libuv's two-callback model:

```cpp
stream.read_start(allocator, reader);
```

This should remain available. It is the most direct shape for callers that need
exact buffer and lifetime control.

The target higher-level API may add a one-callback read form that owns or
recycles buffers internally:

```cpp
client.read_start(uv::read_buffer_policy::allocate(4096),
  [](uv::tcp &client, uv::owned_read_result read) {
    if (read.eof()) {
      client.close();
      return;
    }

    use(read.bytes());
  });
```

This is separate from coroutine support but complements it. The coroutine
surface can then build on the same ownership policy:

```cpp
auto read = co_await client.async_read_some(4096);
```

Do not describe this as a user callback "calling a function to fill the buffer".
libuv owns the asynchronous read sequencing: allocation callback first, native
read second, read callback third. uvpp may hide those two callbacks behind a
single higher-level API, but it cannot turn the read into a synchronous buffer
fill.

## Filesystem Direction

Filesystem is the best initial prototype for operation awaitables.

The existing `uv::fs` layer already owns internal state per operation:

- `raw::request`;
- callback storage;
- copied path strings where needed;
- owned buffers for high-level reads and writes;
- result conversion and `uv_fs_req_cleanup()`.

Coroutine filesystem adapters should reuse this model. The awaitable completion
path should clean up the raw request before resuming or before returning the
owned result, following the same lifetime guarantees as the callback API.

Target names:

```cpp
co_await uv::fs::async_open(loop, path, flags, mode);  // fs::open_result
co_await uv::fs::async_read(loop, file, size, offset); // fs::read_result
co_await uv::fs::async_close(loop, file);              // fs::status_result

co_await uv::fs::open_file(loop, path, flags, mode);   // file_descriptor
co_await uv::fs::read_bytes(loop, file, size, offset); // read_result or owned_buffer
co_await uv::fs::close_file(loop, file);               // void
```

The exact return type of `read_bytes` is still open. Returning `fs::read_result`
preserves byte count and raw status information. Returning `owned_buffer` is
shorter but loses status shape unless EOF and partial reads are represented
elsewhere.

## Networking Direction

Request-based networking operations should follow the one-shot awaitable model:

```cpp
auto status = co_await client.async_connect(addr);
auto written = co_await client.async_write(bytes);
auto closed = co_await client.async_shutdown();
auto sent = co_await socket.async_send(bytes, peer);
```

High-level throwing helpers may use names that imply success or throw:

```cpp
co_await client.connect_to(addr);
co_await client.write_all(bytes);
co_await client.shutdown_stream();
co_await socket.send_to(bytes, peer);
```

Buffer ownership must be explicit:

- high-level write/send helpers may copy payload bytes into operation-owned
  storage;
- lower-level awaitables may borrow spans, but the borrowed lifetime must be
  visible and documented;
- batch UDP sends should not hide metadata allocations in low-level APIs.

`listen`, stream reads, and UDP receive should be treated as event streams, not
ordinary request completions.

## Timer Direction

One-shot sleep is a good coroutine API:

```cpp
co_await uv::sleep_for(loop, 100ms);
co_await uv::sleep_until(loop, deadline);
```

This coroutine sleep must be event-loop based, normally via `uv_timer_t`. It
must not be implemented with `uv_sleep()`: `uv_sleep()` blocks the current
thread, just like `std::this_thread::sleep_for()`, and would prevent
`loop.run()` from dispatching other events on that thread while sleeping.

The awaitable may own an internal timer handle. Because handles are
address-stable and close asynchronously, the implementation must keep the timer
alive until its close callback has run or document a different explicit owner.

Repeating timers should be modeled as event streams or channels rather than as a
single awaitable.

## Handle Close

Awaitable handle close is useful but must be conservative.

Possible shape:

```cpp
co_await uv::async_close(handle);
```

Rules to decide before implementation:

- the handle object must remain alive until the close completion resumes the
  coroutine;
- calling close twice remains a user error;
- close callbacks already installed on the handle need a defined replacement or
  composition policy;
- cancellation of an awaitable close cannot make libuv "unclose" the handle.

Do not add an awaitable close API until those rules are explicit.

## Cancellation

Cancellation is not solved by C++20 coroutines themselves. uvpp should not imply
that destroying an awaitable or task cancels the underlying libuv operation
unless that behavior is actually implemented and documented.

Some libuv operations can be cancelled with `uv_cancel`, but not every operation
or completion state is cancellable. Handles and watchers often require stop or
close semantics instead.

The first coroutine API may choose no implicit cancellation:

- operation state remains alive until libuv completes;
- destroying a task before completion is either disallowed, terminates, or
  detaches by explicit policy;
- cancellation support is added later through explicit tokens or stop handles.

This decision must be documented in `uv::task<T>` and every awaitable family.

## Exception Boundaries

No exception may escape a libuv C callback.

Coroutine awaitable callbacks should follow the same exception boundary as the
existing callback API:

- catch exceptions inside trampolines and callbacks that are invoked by libuv;
- store exceptions in coroutine state when they are part of awaitable result
  delivery;
- resume the coroutine and rethrow from `await_resume()` when appropriate;
- terminate only for truly unexpected exceptions crossing a libuv callback
  boundary.

The exact policy must stay consistent with `core/callback.hpp`.

## Documentation Requirements

When coroutine support is implemented, user documentation must explain:

- how to start a task;
- whether tasks are lazy or eager;
- how the event loop is driven;
- throwing vs explicit-result APIs;
- lifetime rules for handles, requests, and buffers;
- which awaitables own buffers and which borrow them;
- cancellation limitations;
- how stream-like event sources differ from one-shot operations.

Examples should include both filesystem and networking code. Do not publish a
filesystem-only coroutine story as the final design.
