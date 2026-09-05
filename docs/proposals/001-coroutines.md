# Coroutines

Status: draft.

Target: v3. This supersedes the exploratory coroutine strategy previously kept in
`docs/design/`. The API names below are sketches, not available functions.

## Motivation and current behavior

The callback API covers the major asynchronous libuv families, including filesystem,
DNS, work, random, networking requests, and repeated handle events. Users still
organize continuation state and cleanup manually. Coroutines should make sequential
asynchronous work readable while preserving native ownership and costs.

Keep C++20 and header-only integration. Put optional coroutine facilities in focused
headers; ordinary callback users should not need coroutine task or scheduling state.
Keep explicit native access, user-owned `data`, and address-stable handles/requests.

## Proposed task model

Start with a lazy, move-only `task<T>` and `task<void>`. Calling a coroutine constructs
a task; awaiting it or explicitly spawning it starts execution. Specify single
consumption and continuation ownership. The promise stores a value or exception and
transfers control to its continuation at final suspension.

A never-started task may destroy its frame. A running task must remain owned until
all native references to its frame, requests, and borrowed buffers have ended.
Task destruction must not imply successful native cancellation. Prefer explicit
[task scopes](003-cancellation-and-task-scopes.md) for started work; define and test
the contract for destruction without join before stabilizing this type.

Independent execution requires a named spawn/detach API, a surviving owner, and an
explicit error destination. Do not silently detach abandoned work. Bind execution
to a loop context and specify where continuations resume with the
[scheduling proposal](007-loop-scheduling.md). Do not drive nested event loops.

## Operation awaitables

Adapt one-shot operations using the [shared operation protocol](004-operation-state.md):
filesystem operations, DNS, random, work queue, TCP/pipe connect, stream write and
shutdown, UDP send, timer sleep, and handle close.

The awaitable owns request and completion state or borrows it explicitly. Stable
coroutine-frame storage may avoid a separate request allocation, but the frame
itself can allocate and must outlive native completion. Copy necessary path inputs;
represent payload borrowing, copying, and ownership transfer explicitly. A borrowed
handle still requires its owner to survive the operation.

Build and validate more than filesystem adapters before freezing the API. DNS
owned results, work-thread completion, borrowed writes, and asynchronous handle
close exercise different protocols. Clean native resources at the appropriate
family-specific point before result delivery can destroy operation state.

## Error policy

Use one operation vocabulary with an explicit result adapter, as explored in
[errors and results](006-errors-and-results.md), instead of duplicating a throwing
and result-returning name for every operation. Illustrative shape:

```cpp
// Proposed API; task startup and resource ownership are handled by the caller's scope.
auto connected = co_await uv::as_result(client.async_connect(address));
if (!connected) {
  report(connected.error_code());
  co_return;
}
co_await client.async_write(payload.view()); // explicit borrow until completion
```

The exact result type and default policy remain open. The result form must cover
both native submission and completion errors. Specify allocation/setup exceptions
separately and ensure operation construction does not bypass the adapter's contract.
EOF is normal stream control flow, and partial progress must not disappear on error.

No exception may escape a C trampoline. Store operation delivery exceptions where
they can be observed through the task; route unobserved independent-task failures
to the explicit handler. Callback users retain their documented exception boundary.

## Streams and repeated events

One-shot `async_read_some`, accept, and receive adapters are useful but must claim
the relevant native callback slots and reject incompatible simultaneous consumers.
Specify whether a subscription remains active between awaits or starts/stops each
time. Preserve the low-level allocator/reader callback pair.

For repeated reads, signals, timers, and filesystem events, consider an asynchronous
sequence with `next()` and owned or explicitly borrowed values. A channel adds queue
capacity, overflow, producer suspension, close, and error semantics; introduce it
only when those needs justify it. C++23's synchronous `std::generator` does not
provide an asynchronous event sequence. See [buffers and flow control](005-buffers-and-flow-control.md).

## Timers, close, and cleanup

`sleep_for(loop, duration)` and `sleep_until(loop, deadline)` use event-loop timers
and `std::chrono`; never implement them with blocking `uv_sleep()`. Owned timer
storage survives its native close callback. Repeating timers use subscriptions.

Awaitable close cannot release the handle before close completion or replace an
unrelated close callback silently. Cancellation cannot undo close. Resource cleanup
must also work when an exception bypasses the final explicit close expression;
[asynchronous owners](002-async-ownership.md) define that boundary.

## Cancellation and composition

Design stop, deadlines, joining, and abandoned-task behavior before shipping the
public task contract. [Cancellation and task scopes](003-cancellation-and-task-scopes.md)
defines the distinction between requested stop and actual native completion. The
first implementation can expose a limited set of cancellable adapters, provided
unsupported cases and their lifetime behavior are explicit.

## Alternatives and open questions

- Lazy versus eager tasks: lazy is preferred to make startup explicit.
- Loop binding at task creation versus scope startup; behavior of cross-loop awaits.
- Frame allocation customization and optional operation pools after measurement.
- Result-adapter vocabulary and default error behavior.
- Inline resumption versus queued continuations and fairness budget.
- Async sequences versus channels after one-shot adapters are validated.
- External task/executor interoperability without replacing the libuv-oriented core.

## Implementation progress and validation

The existing callbacks, request wrappers, filesystem owners, and coroutine strategy
are foundations. No public `task<T>` or operation-awaitable layer is implemented.

Prototype a timer, filesystem read, DNS lookup, connect/write, worker operation,
and close before committing public names. Test immediate and delayed failures,
never-started tasks, nested awaits, result moves, exceptional cleanup, stop races,
late callbacks, and exactly-once resumption. Check stack growth for long chains.

Publish runnable filesystem and networking examples covering startup, loop driving,
error styles, buffer ownership, and scope exit. Measure frame/request allocations
and compare callback/static/coroutine paths using the
[validation proposal](010-validation-and-performance.md).
