# Cancellation, Deadlines, and Structured Tasks

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Native request cancellation exists in the low-level API, but there is no task tree,
uniform deadline facility, or structured joining of related operations. Coroutine
destruction alone cannot establish that native work has stopped.

## Proposed design

Provide explicit task scopes in `uv::co` with spawn, stop request, and asynchronous
join on the common `uv::loop`. Scope startup consumes cold tasks and binds them to
the scope's loop context, consistently with `spawn(loop, task)` in
[001](001-coroutines.md). Nested awaited cold tasks inherit their parent's context.
Starting a task twice is invalid; cross-loop joining is unsupported initially.
Callback and coroutine operations share cancellation
state and native completion rules; an explicit result API does not change ownership.
A scope owns started children until their native operations and cleanup complete.
A child error requests sibling stop under a documented scope policy; join still
waits for all children. A scope must join children before destroying external data
they borrow; retaining child frames alone cannot preserve that data.

A move-only root spawn handle owns execution and supports asynchronous join and
result/exception observation. Destroying an active handle requests cancellation
without blocking or running the loop recursively. Execution state, native requests,
and required frame storage survive through actual completion and cleanup even
after the public handle is gone. Specify a concrete retention and reclamation
mechanism before shipping; do not assume universal shared ownership. The loop must
remain alive and be driven through this cleanup. Errors that can no longer be
observed through join need an explicit destination under [006](006-errors-and-results.md).

The initial composition surface is task, spawn handle, and task scope. Defer
public detach until ownership of frames and operations, cleanup, and unobserved
failure routing are resolved. Handle destruction follows the cancellation contract
above and does not grant early release of borrowed memory.

Separate cancellation request, native cancellation attempt, operation completion,
and storage reclamation. Support cooperative stop tokens; bridge stop requests
from other threads through [loop scheduling](007-loop-scheduling.md). A cross-thread posting endpoint is an explicit optional capability, not a second
scheduler required by loop-thread scopes. Token
callbacks must not directly mutate loop-owned state from an arbitrary thread.
Ordinary spawn, join, and handle destruction follow loop-thread affinity; any
cross-thread control capability must be explicitly specified by that proposal.

Define cancellation per operation family and supported libuv version. Requests
may attempt native cancellation, read subscriptions may stop, and handle owners
may close only when their ownership contract permits it. Unsupported or unsuccessful
cancellation leaves the operation alive until its actual completion. Retain request
memory and borrowed buffers until that point. See the
[libuv request contract](https://docs.libuv.org/en/v1.x/request.html#c.uv_cancel).

For subscriptions, a stop request alone does not release callback slots. Once stop
has made the subscription terminal and quiesced its native source, release slot
ownership before delivering terminal completion. In-flight callbacks still retain
the state they require; follow [004](004-operation-state.md).

Use monotonic deadlines and `std::chrono`. A deadline requests cancellation; it
cannot promise immediate physical completion or undo external side effects. Record
whether completion or the stop request wins according to a single serialized rule.

Add all-completed and first-completed composition after this contract is tested.
First-completed composition requests loser cancellation and joins losers before
releasing their state. Consequently it may return later than the winning result;
an immediate-return alternative would require an explicit surviving owner.

## Implementation and costs

Share operation completion machinery with [operation state](004-operation-state.md).
Unregister stop callbacks before destroying their state. Start with one timer per
deadline; a shared timer queue is a later measured optimization. Avoid locks on
ordinary loop-thread task transitions.

## Alternatives and open questions

- Choose interoperability with `std::stop_token` versus a loop-specific token.
- Choose fail-fast versus collect-errors scope policies and their result shapes.
- Distinguish timeout, requested stop, native cancellation failure, and I/O failure.
- Define scope destruction without join; no implicit unsafe frame destruction.
- Specify spawn-handle state retention/reclamation after destruction, unobserved
  error routing, join consumption rules, and startup-failure ownership rollback.

## Implementation progress and validation

Native cancellation is available; task scopes, deadline composition, and their
uniform contracts are proposed. Stabilize these contracts before the coroutine API.

Validate stop before submission, concurrent stop/completion, unsuccessful native
cancellation, stop during close, non-cancellable work, late callbacks, sibling
failure, multiple failures, and exactly one result per operation. Use deterministic
ordering controls rather than timing-only sleeps for race coverage.
Also validate context inheritance, active spawn-handle destruction with
non-cancellable work, external borrow lifetimes through scope join, terminal
subscription replacement, and deferred cleanup after the public handle is gone.
