# Asynchronous Resource Ownership

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Low-level handles have stable addresses and require explicit close before
storage destruction. The current [ownership contract](../design/ownership-strategy.md)
is accurate, but every session must implement cleanup across success, submission
failure, callback failure, and cancellation. A final explicit close in the happy
path does not protect earlier exits.

## Proposed design

Place caller-controlled wrappers in `uv::raw` and recommended owners in `uv`.
Add a unique owner for stable handle storage, an awaitable internal
close-completion primitive, and a resource scope. Generic public coroutine close
is a separate decision. A movable owner may transfer its pointer; the native handle
and low-level wrapper remain non-copyable and non-movable. Ownership transfer
must be named or represented by an owning type, never by an implicit borrowed view.

Adoption must transfer an owning resource whose stable storage can actually be
retained. An arbitrary raw reference, particularly a stack wrapper, cannot transfer
that guarantee. Borrowing is separate and never authorizes independent close.
Explicit raw/native access must document permitted mutations and callback-slot
conflicts; a callback-scoped accessor does not enforce these rules by itself.

The scope retains registered resources until close completion, including when a
child task fails. Handle owners borrow the common `uv::loop`; the loop must remain alive and
be driven until cleanup finishes. Explicit asynchronous scope exit is the normal
path. The destructor cannot await, run a nested event loop, or immediately free a
closing handle. Before stabilizing this API, choose a documented fallback for an
owner destroyed without an explicit exit: transfer to an already-live cleanup
scope or diagnose the contract violation. Do not silently leak or detach cleanup.

Lexical destruction may initiate cleanup and hand it to a surviving resource
scope under that contract. It cannot guarantee cleanup has finished before the
next statement. Joining cleanup requires an asynchronous scope boundary, including
on exceptional exits; its public syntax remains open. Retaining owner or task
storage does not extend the lifetime of external borrowed buffers.

The internal primitive claims the close callback slot: start close, call
`uv_close()`, receive its callback, then make storage reclaimable once no remaining
references require it. Deliver close completion exactly once, releasing internal
slot ownership before user code can resume. Reject incompatible existing close
ownership. Repeated cleanup requests on an owner may join the existing completion;
they must not submit another native close. Cancellation cannot undo close or
release its storage early.

## Implementation and costs

Start with one explicit allocation for stable owned handle state. Avoid universal
shared ownership and reference counting. Consider scoped storage or pools only
after measuring this baseline. Apply the same ownership principles to file and
directory resources, while preserving their distinct native cleanup protocols.

Coordinate exceptional scope exit with [structured tasks](003-cancellation-and-task-scopes.md).
Cleanup failure must be observable without discarding the original task failure.
`resource_scope` and `task_scope` are deliberately separate and composable:
the former owns asynchronous resource cleanup, while the latter owns child task
execution. A task receives only a borrowed resource view/reference from the
resource scope; it never takes over an adopted owner. Their server-level
composition is specified in
[011 — Resource scopes](011-resource-scopes.md).

## Alternatives and open questions

- Caller-owned raw storage preserves the direct path without owner allocations.
  Existing wrapper-specific storage costs must still be documented.
- Decide owner construction, release, adoption, and borrowing vocabulary.
- Decide how multiple close waiters and cleanup errors are represented.
- Decide separately whether generic explicit close is a public coroutine operation
  (for example, `co_await socket.close()`); the internal primitive is required either way.
- Choose asynchronous scope-exit syntax and the destruction fallback above.
- Define registration, exceptional exit, and error aggregation for a resource
  scope composed with a task scope.

## Implementation progress and validation

The experimental `uv::tcp_connection` is the first v3 owner slice. It owns one
stable TCP/connect state allocation, moves only that ownership pointer, and has an
internal `open → closing → closed` close state machine. Its internal-only
`uv::detail::close_completion(connection)` awaiter starts `uv_close()` exactly
once, lets later cleanup requests join the pending callback, and checks the
awaiting task's loop affinity. The close callback detaches all waiter claims
before resuming them; state released by an owner remains alive until that delivery
has completed. Coroutine waiters are copied into state-owned continuation storage
at suspension time, so the `noexcept` close callback neither allocates nor follows
linkage in another coroutine frame after its first user resumption. Failed connects
and untransferred accepted connections use a state-owned fixed callback slot for
the same no-allocation callback path. The first TCP-only `resource_scope` now
adopts that owner, hands tasks a non-owning `tcp_connection_view`, and awaits
internal close completion before destroying owner storage. It rejects cross-loop
adoption and requires task join before cleanup while borrowed I/O is active.
A public close awaitable, listener ownership, generic registration, and cleanup
error aggregation remain unimplemented.

The experimental `uv::tcp_listener` adds separate stable listener storage and a
one-shot accept owner transfer. An accepted `tcp_connection` is not owned by the
listener and remains valid while the listener closes. Its receiving task owns it;
an awaited handler can take that owner by value. No resource/task scope currently
relates independently spawned handlers to their accepted connections, so automatic
concurrent `serve(handler)` is intentionally deferred. The scope design must also
own any accepted-connection queue and define its overload policy; the listener does
not silently allocate or buffer connections between one-shot accept awaiters.

The experimental tests use death tests to verify that active connection read/write
and active listener accept destruction terminate deterministically rather than
releasing storage early. They exercise both explicit listener `close()` and lexical
destruction while accept is active.

The slice rejects destruction while its experimental read or write operation is
active: this is an explicit contract violation, diagnosed by an assertion and
termination in release builds. It must not be relaxed until a resource scope and
stop/retention contract can keep operation state alive through actual completion.

Tests cover one close initiation with multiple joining waiters, release of the
close waiter list before resumption, wrong-loop rejection, and owner destruction
while close completion remains awaited. Validate normal exit, exception before the
final close, failed initialization, failed close submission for request-based
resources, cancellation during cleanup, owner moves, callback-slot conflicts, and
loop shutdown with outstanding cleanup. Use sanitizers to verify that storage
survives every native completion.

## Remaining Directory Layer

The former design intentions for a higher-level directory owner belong here. V2
provides raw-only incremental directory iteration; its directory/result destructors
assert ownership has been consumed and do not close resources. Prototype an owning
incremental adapter with cleanup before every next read and close, owned entry names,
and an explicit asynchronous exit. Decide allocation/buffering and failure behavior
before promising a safe default directory API.
