# Explicit Loop Scheduling

Status: partially implemented.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 development on this branch.

This proposal is not completely implemented or an API-freeze commitment. Names
outside the implementation-progress section remain provisional.

## Motivation and current behavior

`uv::async` can wake the loop from another thread, but applications supply their
own queue and synchronization. A reusable posting facility can support explicit cross-thread publication.
Coroutine continuation rules also need definition on the existing loop.

## Proposed design

Use the canonical `uv::loop` for raw callbacks, high-level operations, and
coroutines. Define ordinary loop-thread continuation behavior without requiring
an executor, dispatcher, or hidden runtime. Any fairness queue or wakeup state
needed by this policy must have documented ownership, cost, and shutdown behavior.

Follow the task model in [001](001-coroutines.md): a cold task has no execution
context at construction. Root `spawn(loop, task)` binds it before execution;
nested awaited cold tasks inherit the parent's context. Tasks start only once and
spawned executions are joined through their handles. Cross-loop joining is
unsupported initially, and operations on captured resources must respect the
resources' loop affinity. Context-dependent operations such as coroutine sleep
use the inherited loop without requiring it at each call.

Ordinary spawn, join, and spawn-handle destruction occur on the loop thread.
Binding context does not imply cross-thread publication; any cross-thread control
entry point is a separate explicit capability. Specify whether root startup runs
inline or is scheduled, and define ownership rollback if startup fails.

Separately, add an optional explicit posting component bound to a borrowed
`uv::loop`. Cross-thread posting users opt into this component. It owns a work
queue and an async wakeup handle. It is neither a general executor nor mandatory
state for raw handles or ordinary loop-thread coroutines.

### Selected posting-component contract

The first posting slice has an owner and a separately lifetime-safe producer
endpoint. The owner is created and closed on its loop thread. Endpoints may be
copied or moved to producer threads, but expose no general loop or handle access.
They retain only the posting state; after that state is closed, they are inert and
can safely outlive both the owner and its loop.

`post` transfers ownership of one callable to the component. Its accepted/rejected
outcome is explicit: the ergonomic surface may throw for rejection, while the
corresponding `uv::ops` surface reports a `uv::status`. Preparing the callable or
allocating its queue storage remains ordinary C++ setup and may throw. The exact
public type and spelling remain provisional.

Acceptance linearizes while holding the queue mutex, after the component has
confirmed that it is open, has capacity, and has inserted the callable. The same
critical section coordinates `uv_async_send()` with close initiation; therefore no
accepted producer can send through a handle that has begun native close. If the
wakeup cannot be submitted, the insertion is rolled back and the post is rejected.
Rejected or rolled-back callables are destroyed outside the mutex.

Accepted work executes exactly once on the loop thread, provided the application
continues to drive that loop through posting-component close completion. A producer's
sequentially accepted posts execute in their acceptance order. There is no
meaningful wall-clock ordering promise between concurrent producers. The queue is
bounded in this slice; a full queue rejects without waiting for capacity. Producers
may still contend on the queue mutex.

Wakeups are notifications, not work counts: libuv may coalesce several
`uv_async_send()` calls into one callback. The callback takes a bounded batch from
the synchronized queue, then invokes it without the mutex. If work remains, it
arranges another wakeup. The batch budget is part of the implementation contract:
it bounds posting work per loop turn so a sustained producer load does not starve
I/O. See [libuv async handles](https://docs.libuv.org/en/v1.x/async.html).
Explicit mutex synchronization, rather than an assumed libuv memory-ordering
guarantee, publishes queue entries and coordinates closing. The implementation
must nevertheless be validated against the minimum supported libuv version.

Callable exceptions never escape the async C callback. The component routes them
to its explicitly configured failure handler; an absent handler, or a handler that
throws, terminates. This handler is distinct from the loop's unobserved root-task
failure handler and does not change callback exception policy elsewhere.

Shutdown has the following phases:

```text
open
  -> draining: reject new posts; execute every accepted callable
  -> closing: queue and in-flight drain batches are empty; initiate uv_close()
  -> closed: native close callback has completed
```

Repeated close requests join the same completion. An accepted callable is never
discarded merely because close begins. The owner must remain alive until that
completion; early owner destruction is a diagnosed contract violation, rather
than an implicit nested loop or detached native close. The async handle is kept
referenced while open or draining, so it keeps the loop alive until close begins.

Coroutine continuations normally resume on their associated loop. Define whether
ready operations can resume inline and use a bounded dispatch budget to prevent
long continuation chains from starving I/O. Nested `loop.run()` is not a scheduling
mechanism. Worker callbacks return through the loop rather than accessing handles.

## Implementation and costs

Synchronization belongs to this explicit component, not every handle. Begin with
a simple synchronized queue and move-only callables; optimize only from contention
and latency measurements. Queue nodes and callable erasure may allocate. The first
slice makes that cost explicit and does not claim allocation-free posting; proposal
[008](008-move-only-callbacks.md) remains responsible for a general callback-storage
policy. Document the configured capacity, allocation policy, wakeup cost, and
drain budget with the implemented API.

## Alternatives and open questions

- Select the final public names, construction options, and exact throwing versus
  `uv::ops` entry points after the first implementation validates them.
- Choose the default queue capacity and a configuration shape after measuring
  contention, latency, and allocation behavior.
- Decide whether a future scheduler-owned continuation queue can share any of this
  machinery without weakening its independent fairness and shutdown contract.
- Keep an adaptation boundary for external executors without requiring a general
  sender/receiver framework or a newer language standard.

## Implementation progress and validation

The low-level async handle and the first experimental posting component are
implemented in [`loop_posting.hpp`](../../include/uvpp/loop_posting.hpp).
`loop_posting` owns the bounded synchronized queue and native wakeup handle;
`loop_posting_endpoint` is the copyable producer capability. The component drains
accepted work before close, exposes `close()` as a same-loop coroutine completion,
and reports rejection through `uv::ops::post`. Its exact public names and options
remain experimental. Coroutine continuation policy remains proposed.
The current defaults are capacity 1024 waiting callables and drain budget 64.
The reusable batch is reserved before native initialization; callback delivery
retains native state through waiter resumption and releases handler captures at
close. The [posting tests](../../tests/test-loop-posting.cpp) now cover live
producers and per-producer FIFO, coalesced pre-run wakeups, repeated admission/close
races with exact accepted/delivered comparison, full/closed rejection, endpoints
outliving the owner and loop, reentrant capture destruction, handler replacement,
multiple close waiters with owner release, timer progression through a backlog,
setup rejection, affinity, and terminating misuse/failure policies.
`make test-tsan-posting` supplies focused queue/endpoint race instrumentation;
ASan/UBSan and the ordinary GCC/Clang suites cover the same lifecycle tests.
See [the loop guide](../user/loop.md#cross-thread-posting) and
[thread safety](../design/thread-safety.md#posting-synchronization-and-retention)
for implemented contracts and costs.

Supported-version/platform validation, failure injection for native send and
allocation rollback, and contention/latency measurements remain future gates.
Validate the ordinary coroutine layers on one loop without a separately constructed
posting component before freezing their API. For the component, validate many
producers, coalesced wakeups, posts racing with shutdown, rejection, loop-thread
identity, exception routing, bounded queue behavior, drain-before-close, endpoint
lifetime after close, and I/O fairness.
Also validate context inheritance before first child execution, rejected cross-loop
joins and resource-affinity mismatches, and continued loop driving through cleanup
after active spawn-handle destruction.
Run thread sanitizer on the queue and endpoint lifetime tests where supported.
