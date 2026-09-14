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

TCP and pipe now share private stream-operation awaiters backed by a common
`stream_io_state`. Each family provides only its native `uv_stream_t`, execution
loop, I/O-slot state, and explicit native-handle recovery; this keeps application
`uv_handle_t::data` untouched. The common one-shot `read_some()` protocol checks
state, affinity, and pre-existing stop; claims allocation/read slots; starts the
native source; then registers cancellation. Data, EOF, errors, and completed
cancellation quiesce with `uv_read_stop()`, release allocation/read and
cancellation slots, and only then resume the task. Zero-byte notifications remain
armed. An unexpected stop failure terminates rather than releasing a callback slot
that a later native callback could use after its coroutine frame has gone away.

The shared borrowed `write()` similarly performs state, affinity, and
pre-existing-stop checks, claims one `uv_write_t` slot, and clears it on submission
failure or native completion before resumption. It stores the native buffer view,
not the bytes it borrows. TCP and pipe retain only their family-specific
connect/accept/endpoint behavior around that common stream protocol; pipe's close
state machine separately snapshots joined close waiters before the first user
resumption.

The experimental IPC-pipe `write_with_handle()` uses that same exclusive write
protocol, submitting `uv_write2()` only after it has pinned the sent TCP owner's
stable state. Submission failure and completion release both the pipe write slot
and this export pin before user delivery. `receive_handle(buffer)` is a separate
one-shot read protocol: it reserves the future TCP state before `uv_read_start()`,
continues through byte notifications until libuv exposes at least one pending
native handle, then validates and adopts TCP handles with `uv_accept()` before
quiescing and releasing read/cancellation slots. This is deliberately not a
message-framing API: byte callbacks and pending handles have no one-to-one
association. The caller buffer accumulates every byte received while waiting; its
returned count must not be interpreted as the payload belonging to an adopted
handle. Filling that buffer before a handle arrives fails with `UV_ENOBUFS`. The callback drains
the entire native pending queue into the move-only `received_handle` result;
`take_tcp()` then extracts one high-level owner at a time. This avoids depending
on a future byte notification to expose a queued sibling. An unsupported pending
type, or a native adoption failure, fails closed with an error and closes the
pipe; an initialized child that cannot be adopted is also closed before its task
receives the error. The awaiter reserves the first child before the read; a burst
may require additional high-level owner allocations in the native callback. Those
allocations are guarded and turn into `UV_ENOMEM` plus fail-closed cleanup rather
than allowing an exception to escape libuv. The result never exposes a raw pointer.

TCP and pipe listeners share a private `accept_slot` for their persistent
`uv_listen()` sources. The slot owns only the exclusive one-shot high-level claim
and its delivery/cancellation function pointers. A notification or cleanup first
detaches all three slot pointers, then invokes family-specific delivery or
cancellation; a resumed coroutine may therefore destroy listener state without a
later slot dereference. Each listener still constructs its own provisional child,
calls `uv_accept()`, and owns its child-close path. An accept error closes that
initialized storage before task delivery; success transfers it to the resumed
task as the family-specific connection owner. Listener destruction while an
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

TCP, pipe, and UDP owners now use a private `async_close_state` for their common
internal close bookkeeping rather than awaiters directly around `uv_close()`.
Its state-owned continuation storage makes a repeated cleanup request join an
already submitted close. A coroutine waiter is copied into that storage before
suspension; the native callback snapshots every waiter, marks the state closed,
then resumes from the snapshot. It therefore neither allocates nor follows links
in another coroutine frame after the first user resumption, and only then releases
state whose owner was destroyed. The family still owns native storage, pre-close
quiescence, and the actual `uv_close()` call. Internal completion entry points
check loop affinity and are not public `close()` APIs.

Validate failure at each setup stage, native submission failure, absent callbacks,
resubmission from completion, destruction from completion, owned-result extraction,
and exactly-once cleanup. Exercise DNS, filesystem, write, and close before calling
the internal protocol stable. Compare allocation counts with the existing paths.
For subscriptions, validate immediate replacement from terminal user delivery on
EOF, error, cancellation, and stop; ordinary-event slot retention; setup rollback;
and late callbacks that neither resume twice nor clear replacement slots. Test both
callback and coroutine frontends, including destruction during terminal delivery.
