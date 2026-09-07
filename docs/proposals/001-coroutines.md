# Coroutines

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

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

Put coroutine primitives in `uv::co`; high-level I/O objects remain in `uv`.
Do not introduce coroutine-specific socket or filesystem owner hierarchies.
Use cold, move-only `uv::co::task<T>` and `uv::co::task<void>`. A task has no
execution context at construction. Starting a root through `spawn(loop, task)`
consumes the task and binds its execution context to that loop. Awaiting a cold
child consumes it and propagates the parent's execution context before it starts.
A task may be started only once; awaiting an already-started task is invalid even
on the same loop. Join a spawned execution through its spawn handle instead.
Cross-loop joining is unsupported initially.

```cpp
uv::co::task<int> foo(); // cold; no loop parameter required

// In application startup code:
auto h = uv::co::spawn(loop, foo());
```

Capturing a loop-bound resource does not bind the task at construction or change
the resource's affinity: its use must agree with the task's execution loop.
Specify continuation ownership. The promise stores a value or exception and
transfers control to its continuation at final suspension.

A never-started task may destroy its frame. A running task must remain owned until
all native references to its frame, requests, and borrowed buffers have ended.
Task destruction must not imply successful native cancellation. Prefer explicit
[task scopes](003-cancellation-and-task-scopes.md) for started work; define and test
the contract for destruction without join before stabilizing this type.

The proposed root-starting shape is:

```cpp
namespace uv::co {
template<class T>
[[nodiscard]] spawn_handle<T> spawn(uv::loop&, task<T>);
} // Exact join/stop member names remain provisional.
```

A move-only spawn handle owns the root execution and supports asynchronous join
and observation of its result or exception. Destroying an active handle requests
cancellation without blocking. It does not immediately destroy a frame still
referenced by native work: execution state survives through actual completion and
required cleanup. Before implementation, specify who retains and reclaims that
state after handle destruction and where unobserved failures go; see
[003](003-cancellation-and-task-scopes.md) and [006](006-errors-and-results.md).
Retaining a frame does not extend the lifetime of external objects it borrows.

Start with task, spawn handle, and task scope. Defer public detach until frame and
operation ownership, cleanup, and error routing have explicit contracts. The
cancellation-and-retention contract for handle destruction is not a public detach
operation. Specify continuation placement with the
[scheduling proposal](007-loop-scheduling.md); do not drive nested event loops.

## Operation awaitables

Adapt one-shot operations using the [shared operation protocol](004-operation-state.md):
filesystem operations, DNS, random, work queue, TCP/pipe connect, stream write and
shutdown, UDP send, and timer sleep. Ownership and scope cleanup additionally
require an awaitable internal close-completion primitive; a generic public close
await is a separate API decision.

The awaitable owns request and completion state or borrows it explicitly. Stable
coroutine-frame storage may avoid a separate request allocation, but the frame
itself can allocate and must outlive native completion. Copy necessary path inputs;
use borrowing by default for write/send payloads, with explicit semantic names or
types for copying and ownership transfer as specified in
[005](005-buffers-and-flow-control.md). A borrowed handle still requires its owner
to survive the operation.

Build and validate more than filesystem adapters before freezing the API. DNS
owned results, work-thread completion, borrowed writes, and asynchronous handle
close exercise different protocols. Clean native resources at the appropriate
family-specific point before result delivery can destroy operation state.

## Error policy

Use the structural policy from [006](006-errors-and-results.md): ergonomic
high-level awaits throw on operational errors, while `uv::ops` awaits return
explicit results on the same owners. Illustrative shape only:

```cpp
auto connected = co_await uv::ops::connect(client, address);
if (!connected) {
  report(connected.error_code());
  co_return;
}
co_await client.write(payload.view()); // borrow bytes until actual completion
```

Exact signatures and result accessors remain provisional. Both native submission
and completion failures must follow the chosen channel. Allocation/setup failures
need a separate explicit contract. Share internal result adaptation instead of
requiring `as_result`, `async_*`, and suffix variants as parallel public choices.
EOF is normal stream control flow, and partial progress must not disappear on error.

No exception may escape a C trampoline. Store operation delivery exceptions where
they can be observed through the task; route unobserved independent-task failures
to the explicit handler. Callback users retain their documented exception boundary.

## Streams and repeated events

One-shot `read_some`, accept, and receive adapters are useful but must claim
the relevant native callback slots and reject incompatible simultaneous consumers.
Specify whether a subscription remains active between awaits or starts/stops each
time. Preserve the low-level allocator/reader callback pair.

At terminal completion, quiesce the native event source and release all claimed
callback slots before resuming user code. This includes EOF, terminal error,
completed cancellation, and explicit stop, not a stop request alone. An ordinary
`next()` result does not terminate a persistent subscription or release its slots.
Retain state required by in-flight callbacks and never clear a replacement
subscription's slots after user resumption; see [004](004-operation-state.md).

For repeated reads, signals, timers, and filesystem events, consider an asynchronous
sequence with `next()` and owned or explicitly borrowed values. A channel adds queue
capacity, overflow, producer suspension, close, and error semantics; introduce it
only when those needs justify it. C++23's synchronous `std::generator` does not
provide an asynchronous event sequence. See [buffers and flow control](005-buffers-and-flow-control.md).

## Timers, close, and cleanup

Within a task, `sleep_for(duration)` and `sleep_until(deadline)` use the inherited
execution context, event-loop timers, and `std::chrono`; these names remain
provisional. Never implement them with blocking `uv_sleep()`. Owned timer
storage survives its native close callback. Repeating timers use subscriptions.

Implement an awaitable internal close-completion primitive required by ownership
and scope cleanup: start close, call `uv_close()`, receive the native callback,
then permit storage reclamation once no remaining references require it. It must
not replace unrelated close ownership. Cancellation cannot undo close.

Decide separately whether a generic explicit `co_await socket.close()` is public.
Cleanup must cover exceptional exits without relying on a final explicit close.
A normal C++ destructor cannot await: lexical exit may initiate cleanup under a
surviving resource scope, while joining it requires an asynchronous scope boundary.
[Asynchronous owners](002-async-ownership.md) define that boundary; its syntax is open.

## Cancellation and composition

Design stop, deadlines, joining, and abandoned-task behavior before shipping the
public task contract. [Cancellation and task scopes](003-cancellation-and-task-scopes.md)
defines the distinction between requested stop and actual native completion. The
first implementation can expose a limited set of cancellable adapters, provided
unsupported cases and their lifetime behavior are explicit.

## Alternatives and open questions

- Exact spawn-handle join/stop vocabulary and result-consumption rules.
- State retention and unobserved-error routing after spawn-handle destruction.
- Public generic close exposure and asynchronous resource-scope syntax.
- Frame allocation customization and optional operation pools after measurement.
- Explicit-operation initiation and facade spellings under proposal 006.
- Inline resumption versus queued continuations and fairness budget.
- Async sequences versus channels after one-shot adapters are validated.
- External task/executor interoperability without replacing the libuv-oriented core.

## Implementation progress and validation

The existing callbacks, request wrappers, filesystem owners, and coroutine strategy
are foundations. An experimental focused-header slice now provides `task<T>`, root
`spawn(loop, task<void>)`, move-only child-task await, `spawn_handle::rethrow_if_failed()`,
and `sleep_for(duration)`; see [the coroutine guide](../user/coroutines.md) and
[`tests/test-co.cpp`](../../tests/test-co.cpp). It validates cold root startup,
inherited child loop binding, timer completion/close before resumption, move-only
result delivery, and task failure propagation. It deliberately has no cancellation,
task scope, or asynchronous join, so it does not settle the public task contract.

Set the minimum buffer lifetime contracts from 005 before prototyping. Use the
prototype to revise the initial 002/003/004/006/007 contracts, rather than waiting
for their full implementation. Ordinary loop-thread coroutine use must not require
a separate dispatcher; validate coexistence with raw and high-level callbacks.

Prototype a timer, filesystem read, DNS lookup, connect/write, worker operation,
and internal close completion before committing public names. Test immediate and
delayed failures,
never-started tasks, inherited loop context, single consumption, rejected cross-loop
joins and resource-affinity mismatches, active spawn-handle destruction, result moves,
exceptional cleanup, terminal subscription replacement, stop races,
late callbacks, and exactly-once resumption. Check stack growth for long chains.

Publish runnable filesystem and networking examples covering startup, loop driving,
error styles, buffer ownership, and scope exit. Measure frame/request allocations
and compare callback/static/coroutine paths using the
[validation proposal](010-validation-and-performance.md).
