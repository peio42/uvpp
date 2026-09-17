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
close-completion primitive, a public close operation, and a resource scope. A
movable owner may transfer its pointer; the native handle and low-level wrapper
remain non-copyable and non-movable. Ownership transfer must be named or
represented by an owning type, never by an implicit borrowed view.

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

The private `uv::detail::async_close_state` factors only this common bookkeeping:
the `open → closing → closed` phase, joined coroutine waiters, the fixed
callback-origin waiter, owner-release retention, and release-before-resume
delivery. It does not store a native handle or call `uv_close()`. Each family
retains its own `request_close()` transition, pre-close quiescence, and native
close callback entry point.

## Public owner close

The target v3 high-level owner API exposes an awaitable `close()` on
`tcp_connection`, `pipe_connection`, `udp_socket`, `tcp_listener`, and
`pipe_listener`:

```cpp
co_await connection.close();
co_await listener.close();
co_await socket.close();
```

`close()` is a cold, idempotent native-close completion barrier. Constructing its
awaiter has no effect. Its first await on an open owner starts the family-specific
close transition; later awaiters join that same transition. It completes only
after libuv has delivered the native close callback and the close callback has
released its internal waiter ownership. Awaiting a closed owner completes
immediately after its loop-affinity check. An empty or moved-from owner reports
`UV_EBADF`.

The ergonomic member await throws operational failures at the await expression.
`uv::ops::close(owner)` provides the corresponding explicit-result operation,
following the error-policy boundary in [006](006-errors-and-results.md). The
native close path for these handle families has no completion status of its own;
the initial result vocabulary need only represent precondition and setup failures.
Neither surface promises `noexcept`: joining an in-progress close may allocate
continuation storage.

`request_close()` is the explicit non-awaiting form. It requests the same close
transition but gives no completion guarantee. It is intended for code that has a
separate lifetime or scope boundary to await the completion. Views and raw/native
accessors never acquire independent close authority.

Close is non-cancellable once requested. A pending stop request does not permit
the close waiter to resume early or storage to be released early; the waiter
resumes only after the close completion protocol. Close also does not perform a
protocol shutdown, deliver queued stream writes, or join application tasks.
Those actions remain explicit composition above the owner:

```cpp
co_await workers.join();
co_await connection.close();
```

Close preserves the loop-thread-affinity rule. Awaiting it from a task bound to a
different loop is a contract failure before it changes the owner state. A public
close awaiter borrows its owner until it is awaited: it may not be retained across
the owner's destruction or move. Once registered, the existing close-completion
protocol retains the stable state through callback delivery, including if owner
destruction releases that state.

### Active operations

The initial public contract is limited to quiescence that has been demonstrated
by the owner family. A TCP or pipe connection rejects close with `UV_EBUSY` while
a read, write, or IPC-handle-export operation is active. A UDP socket rejects
close with `UV_EBUSY` while a send or receive is active. These rejections do not
start native close or alter the active operation.

A TCP or pipe listener may close with one active `accept()`. Its family-specific
transition first quiesces that accept and releases its callback ownership, then
starts native close. The accept's own provisional-child cleanup completes before
it reports cancellation. Closing a listener therefore stops admission but does
not join handler tasks or close connections that were already accepted.

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
the same no-allocation callback path. The first `resource_scope` now
adopts connections and listeners, hands tasks a non-owning `tcp_connection_view`,
and awaits internal close completion before destroying owner storage. It rejects
cross-loop adoption and requires task join before cleanup while borrowed I/O is
active. The public close contract is selected above but its awaitable facade,
generic registration, and cleanup-error aggregation remain unimplemented.

The experimental `uv::tcp_listener` adds separate stable listener storage and a
one-shot accept owner transfer. Its internal close completion has the same
`open → closing → closed` protocol as a connection: it cancels/quiesces an active
accept and releases its callback slot before native close. `resource_scope` owns
the listener through that callback and exposes a non-owning registration with
`accept()`. An accepted connection is separately adopted before its handler gets a
borrowed view. Queueing and overload policy remain deferred; the listener does not
silently allocate or buffer connections between one-shot accept awaiters.

The experimental `uv::udp_socket` is the second stable-owner slice. It binds an
address during construction, moves only stable `uv_udp_t` storage, and has the
same internal-only `open → closing → closed` close-completion state machine.
`send_to()` borrows its payload through actual `uv_udp_send` completion; one
one-shot `recv_from()` borrows caller storage, copies the peer address into its
result, stops native receive, releases its callback/cancellation slots, then
resumes. Active UDP send/receive destruction remains an explicit contract
violation, and resource-scope cleanup rejects it until task work has joined.

The experimental `uv::pipe_connection` applies the same stable-owner protocol to
one outgoing local pipe connection. `connect(name, ipc)` copies the name into
the awaiter, preserves its address-stable `uv_pipe_t` state through completion
failure cleanup, and distinguishes an immediate `uv_pipe_connect2()` submission
error where that libuv API is available. Its one-shot `read_some()` and `write()`
borrow caller buffers through native completion, reject concurrent readers or
writers, and check task-loop affinity. An IPC-enabled connected pipe now also
prototypes `write_with_handle(data, tcp_connection&)` and
`receive_handle(buffer)`: the former borrows and pins the source TCP owner through
`uv_write2()` completion, while the latter pre-allocates and adopts one pending
TCP handle into a stable uvpp owner before resuming. It is an unframed control
primitive: byte delivery and native-handle availability have no one-to-one
association; caller storage accumulates bytes while waiting and fails with
`UV_ENOBUFS` if exhausted first. All TCP handles pending in the delivering callback
become stable owners in the returned result, while an unsupported native handle
fails the pipe closed. This is native capability passing, not transfer of the source C++ owner;
pipe/UDP handle families and listener transfer remain future work. `uv::pipe_listener` binds one local
name with a non-IPC listening native handle; its `ipc` option configures the
connected children accepted from that listener. It owns a separate stable listener state, and transfers each one-shot
`accept()` into an independent `pipe_connection`. Its internal close primitive
quiesces a pending accept, closes the provisional child before `UV_ECANCELED`
delivery, then waits for listener close completion. A
`resource_scope` can adopt the owner and exposes only `pipe_connection_view` to
tasks; it can also own a `pipe_listener` and safely quiesce its one-shot accept.
Active borrowed stream I/O must have joined before cleanup starts.

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
while close completion remains awaited. Public-close validation additionally
requires cold construction, closed-owner completion, a stop request before and
during close, and `UV_EBUSY` rejection without mutating active connection or UDP
I/O. It must exercise listener close with a pending accept, including provisional
child cleanup before cancellation delivery. Validate normal exit, exception before
the final close, failed initialization, failed close submission for request-based
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
