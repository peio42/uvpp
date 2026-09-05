# Explicit Loop Scheduling

Status: draft.

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

`uv::async` can wake the loop from another thread, but applications supply their
own queue and synchronization. A reusable posting facility would also give
coroutines a defined place to resume.

## Proposed design

Add an explicit scheduler or dispatcher object bound to a borrowed loop. It owns
a work queue and an async wakeup handle. Posting transfers a callable into the
queue; accepted work executes on the loop thread. Specify per-producer ordering
and a linearization point for acceptance. Do not claim meaningful wall-clock
ordering between concurrent producers.

Drain the queue on wakeup; do not interpret wakeups as task counts because libuv
can coalesce sends. See [libuv async handles](https://docs.libuv.org/en/v1.x/async.html).
Use explicit queue synchronization and verify its correctness against the minimum
supported libuv version rather than assuming a newer memory-ordering guarantee.

Make shutdown explicit: stop accepting posts, settle accepted work according to
a documented drain policy, then close the wakeup handle. Producer access must end
or use a separately lifetime-safe posting endpoint before dispatcher destruction.
Report posts rejected by shutdown or capacity limits through a defined status.

Coroutine continuations normally resume on their associated loop. Define whether
ready operations can resume inline and use a bounded dispatch budget to prevent
long continuation chains from starving I/O. Nested `loop.run()` is not a scheduling
mechanism. Worker callbacks return through the loop rather than accessing handles.

## Implementation and costs

Synchronization belongs to this explicit component, not every handle. Begin with
a simple synchronized queue and move-only callables; optimize only from contention
and latency measurements. Document queue capacity, allocation policy, wakeup cost,
and whether the scheduler's handle keeps the loop alive.

## Alternatives and open questions

- Compare a separate scheduler with adding posting state directly to `loop`.
- Define callable exception routing together with the errors proposal.
- Choose shutdown behavior for accepted work and scheduler-owned continuations.
- Keep an adaptation boundary for external executors without requiring a general
  sender/receiver framework or a newer language standard.

## Implementation progress and validation

The async handle exists; the queue, scheduler, and shutdown protocol are proposed.
Validate many producers, coalesced wakeups, posts racing with shutdown, rejection,
loop-thread identity, exception routing, bounded queue behavior, and I/O fairness.
Run thread sanitizer on the queue and endpoint lifetime tests where supported.
