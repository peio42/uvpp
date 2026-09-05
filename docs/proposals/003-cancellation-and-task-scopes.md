# Cancellation, Deadlines, and Structured Tasks

Status: draft.

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Native request cancellation exists in the low-level API, but there is no task tree,
uniform deadline facility, or structured joining of related operations. Coroutine
destruction alone cannot establish that native work has stopped.

## Proposed design

Provide explicit task scopes with spawn, stop request, and asynchronous join.
A scope owns started children until their native operations and cleanup complete.
A child error requests sibling stop under a documented scope policy; join still
waits for all children. Unobserved detached failures require an explicit handler.

Separate cancellation request, native cancellation attempt, operation completion,
and storage reclamation. Support cooperative stop tokens; bridge stop requests
from other threads through [loop scheduling](007-loop-scheduling.md). Token
callbacks must not directly mutate loop-owned state from an arbitrary thread.

Define cancellation per operation family and supported libuv version. Requests
may attempt native cancellation, read subscriptions may stop, and handle owners
may close only when their ownership contract permits it. Unsupported or unsuccessful
cancellation leaves the operation alive until its actual completion. Retain request
memory and borrowed buffers until that point. See the
[libuv request contract](https://docs.libuv.org/en/v1.x/request.html#c.uv_cancel).

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

## Implementation progress and validation

Native cancellation is available; task scopes, deadline composition, and their
uniform contracts are proposed. Stabilize these contracts before the coroutine API.

Validate stop before submission, concurrent stop/completion, unsuccessful native
cancellation, stop during close, non-cancellable work, late callbacks, sibling
failure, multiple failures, and exactly one result per operation. Use deterministic
ordering controls rather than timing-only sleeps for race coverage.
