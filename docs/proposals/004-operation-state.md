# Shared Asynchronous Operation State

Status: draft.

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Requests already provide submission rollback helpers and one-shot callback
extraction; the public filesystem API owns operation state. Separate coroutine
implementations could duplicate these subtle contracts and drift from callbacks.

## Proposed design

Build a small internal operation protocol shared by callback and coroutine
frontends: prepare inputs, submit, record completion, clean native resources, and
deliver the result exactly once. The protocol owns or explicitly borrows every
input needed after submission. It does not consume libuv `data`.

Document family-specific state transitions, including submission failure without
a future callback. Clear request callback slots and obsolete input storage before
calling user code, so supported resubmission from completion remains safe.

Separate result materialization from raw cleanup: owned results survive cleanup;
borrowed results retain their documented owner and validity interval. Do not apply
filesystem cleanup rules mechanically to streams, DNS, or handle close.

Complete all internal work that needs the state before calling a user callback or
resuming a coroutine: delivery may destroy that state. Specify whether initiation
can complete inline and ensure the awaiter cannot be resumed twice or accessed
after destruction. All C trampolines remain exception boundaries.

## Implementation and costs

Keep native storage and conversion in the existing small storage base, preserving
its layout assertions and native-to-wrapper round-trip tests. Low-level request
ownership remains caller-controlled. Coroutine requests can reside in stable frame
storage when that frame is guaranteed to survive native completion; otherwise use
explicit separately owned state. Do not assume allocation elision.

Share narrow helpers and invariants, not a universal virtual operation hierarchy.
Repeated subscriptions need their own protocol rather than a reused one-shot state
machine. Frontend-specific delivery should not introduce an intermediate
`std::function` when a direct continuation is sufficient.

## Alternatives and open questions

- Compare helper functions with a constrained operation-state concept.
- Determine which result factories can fail and how that failure is delivered.
- Specify setup rollback when allocating input storage fails before submission.
- Choose inline versus queued continuation policy with the scheduler proposal.

## Implementation progress and validation

Current request helpers and filesystem operation owners implement parts of the
mechanism. No common coroutine frontend is implemented.

Validate failure at each setup stage, native submission failure, absent callbacks,
resubmission from completion, destruction from completion, owned-result extraction,
and exactly-once cleanup. Exercise DNS, filesystem, write, and close before calling
the internal protocol stable. Compare allocation counts with the existing paths.
