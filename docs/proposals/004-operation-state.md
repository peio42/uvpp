# Shared Asynchronous Operation State

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Requests already provide submission rollback helpers and one-shot callback
extraction; the public filesystem API owns operation state. Separate coroutine
implementations could duplicate these subtle contracts and drift from callbacks.

## Proposed design

Build a small internal operation protocol shared by ergonomic `uv`, explicit-result
`uv::ops`, and callback/coroutine
frontends: prepare inputs, submit, record completion, clean native resources, and
deliver the result exactly once. The protocol owns or explicitly borrows every
input needed after submission. It does not consume libuv `data`.

Document family-specific state transitions, including submission failure without
a future callback. Clear request callback slots and obsolete input storage before
calling user code, so supported resubmission from completion remains safe.

Extend terminal slot-release ordering to subscriptions and event sources, using
their own repeated-event protocol. Once an operation or subscription is terminal,
quiesce its native source and release all callback-slot ownership before invoking
user callbacks or resuming user coroutines. For read/receive subscriptions this
includes the allocation and read/receive slots. Terminal causes include EOF,
terminal error, completed cancellation, and explicit stop. A cancellation request
alone is not terminal completion, and an ordinary `next()` result does not release
the slots of a persistent subscription.

Slot release and storage reclamation are distinct: retain the state and buffers
still required by in-flight native callbacks or a currently executing trampoline.
The old completion path must never clear or modify slots acquired by a replacement
subscription after user delivery. Complete slot cleanup before delivery and make
any remaining callback unwinding independent of the replacement's state. Each
family must define the quiescence point that makes slot reuse safe.

Keep the libuv binding in raw primitives or narrow internal helpers; `uv::co`
must not reimplement it. Error facade selection must not alter native submission,
ownership, or cleanup. Define deferred or policy-aware initiation so native
submission errors cannot escape the explicit-result channel during construction.
All frontends operate on the same `uv::loop` without a required dispatcher.

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
mechanism. The experimental TCP connect awaiter owns stable `uv_connect_t` and
`uv_tcp_t` storage, distinguishes submission from completion errors, and closes
before delivering a failed connection. It is a family-specific prototype, not yet
a common coroutine frontend.

The same TCP slice now has one borrowed `uv_write_t` awaiter per connection. A
submission failure clears its writer claim without a callback; terminal completion
clears the claim before resuming the task. It stores only the native buffer view,
not the bytes it borrows.

Its experimental `read_some` claims both native read slots. Data, EOF, and errors
call `uv_read_stop()`, clear the alloc/read claim, and only then resume the task;
zero-byte notifications remain armed. libuv guarantees that `uv_read_stop()`
prevents later read callbacks; its non-zero TTY/Windows return is not a failure,
so it does not alter this TCP-only terminal protocol. The next `read_some` may
therefore be started by resumed code without an old callback clearing its claim.

The TCP listener slice treats `uv_listen` as a persistent native source with one
exclusive high-level accept waiter. On a connection notification it first releases
that waiter claim, then calls `uv_accept` into separately stable connection storage.
An accept error closes that initialized storage before task delivery; success
transfers it to the resumed task as `tcp_connection`. Listener destruction while an
accept is active is an explicit experimental contract violation until scopes and
cancellation can complete the operation safely. A notification with no active
waiter is deliberately ignored in this one-shot slice: it neither accepts nor
queues a connection. The eventual scoped server must replace that limitation with
an explicit queue/backpressure policy.

The first `uv_accept()` following a successful libuv connection notification is
guaranteed to succeed, so a deterministic native accept-failure integration test
requires a fault-injection seam not present in this prototype. The current suite
instead verifies an accepted peer that closes immediately, while the error path
remains specified and isolated for future fault injection.

Scoped cooperative cancellation now gives the TCP one-shot read and accept
awaiters an internal registration. Stop quiesces read with `uv_read_stop()` or
releases the listener accept claim, then clears the registration/claimed slots
before resuming with `UV_ECANCELED`; cancelled accept also closes its initialized
but untransferred client storage before delivery. TCP write has no physical native
cancellation here: it checks a pre-existing stop before submission but otherwise
retains its claim and borrowed buffer through write completion.

TCP owner operations also verify that the awaiting task inherited the exact loop
recorded by the owner. This is a high-level affinity check; raw APIs remain
caller-controlled.

The TCP close path now uses a family-specific internal state machine rather than
an awaiter directly around `uv_close()`: `open`, `closing`, and `closed`. Its
intrusive waiter list makes a repeated cleanup request join an already submitted
close. The native close callback first detaches that list and marks the state
closed, then resumes waiters, and only afterwards releases state whose owner was
destroyed. The internal completion entry point checks loop affinity and is not a
public `tcp_connection::close()` API.

Validate failure at each setup stage, native submission failure, absent callbacks,
resubmission from completion, destruction from completion, owned-result extraction,
and exactly-once cleanup. Exercise DNS, filesystem, write, and close before calling
the internal protocol stable. Compare allocation counts with the existing paths.
For subscriptions, validate immediate replacement from terminal user delivery on
EOF, error, cancellation, and stop; ordinary-event slot retention; setup rollback;
and late callbacks that neither resume twice nor clear replacement slots. Test both
callback and coroutine frontends, including destruction during terminal delivery.
