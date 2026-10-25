# Callback strategy

This page records callback machinery present on the v3 branch and the boundary
with the unfinished raw/high-level migration. See
[proposal 004](../proposals/004-operation-state.md) for the shared-operation target.

## Current dispatch

Low-level wrappers provide runtime callables and selected `template<auto Callback>`
static entry points. Runtime slots use copyable `std::function`; move-only callable
storage remains in [proposal 008](../proposals/008-move-only-callbacks.md).
Static dispatch avoids storing a callable, but does not remove string preparation,
native work, wrapper slot size, or other operation costs.

Raw trampolines reconstruct state from native storage layout without consuming
application-owned `data`. High-level TCP/pipe connections and listeners, UDP
sockets, signal sources, and processes bind handle `data` to their stable owner
state. Internal connect requests
use the same state pointer; stream-write and UDP-send requests bind request `data`
to their stable awaiter at submission. These callbacks do not depend on state
layout or member offsets. `loop_posting` also uses implementation-reserved `data`.
High-level owners have their own stable storage;
never recover a low-level wrapper from a high-level owner's native pointer.

`detail::invoke_callback` and static invocation catch escaping user exceptions and
terminate. Native callbacks are `noexcept`. Coroutine promises capture task exceptions
for observation at awaits or through spawn handles; exceptions never cross C frames.

## Reconstruction choices

High-level owners and internal operations may reserve native `data`; they should
use it when it removes an artificial dependency on object layout. The legitimate
choices are:

- raw wrappers use layout/`native_storage` and leave `data` application-owned;
- persistent high-level owners preferably bind `data` to their stable state;
- internal one-shot operations use an awaiter/state pointer in `data` or
  `native_storage`, whichever is simpler without unnecessary constraints.

`sleep_awaiter` binds its initialized timer's `data` to the awaiter before native
start. Timer delivery, cancellation, and start-failure cleanup retain that pointer
through native close; only the close callback resumes the suspended frame.
Reconstruction no longer requires a first-member timer or standard-layout awaiter.
Other internal one-shot operations may keep their existing storage mechanism.

## One-shot completion

Request terminal callbacks extract and clear their callable before invoking user
code. Release or transfer owned native results even when no callable is installed.
Clear submission inputs before user delivery when their lifetime has ended, and
avoid touching request state after delivery: the user may destroy or reuse it.

Existing submission helpers roll callback/input state back when native submission
fails, but do not cover every earlier allocation or callable-construction failure.
Outside migrated filesystem/watchers, they still throw on native failure and
cannot be copied unchanged into the
v3 raw explicit-result surface.

## Persistent slots and owner operations

Persistent low-level callbacks and one-shot request extraction are distinct protocols.
Do not infer safe replacement or rollback from a similarly named start method:
audit the concrete slot and native start/stop behavior. Existing low-level APIs
are not yet a uniform v3 subscription frontend.

The v3 network-owner operations claim exclusive slots. A terminal read/receive
stops its native source and releases read/allocation/cancellation claims before
resuming the task. A submitted write/send retains its claim and borrowed payload
until actual native completion, even after stop. IPC variants share the stream
slots; see [I/O concurrency](io-concurrency.md).

A future persistent subscription must retain exclusivity between ordinary events
and release it only after terminal quiescence. Keep callback state alive through
in-flight native delivery, and prevent an old completion from clearing a slot
installed by resumed user code. This broader frontend remains proposed.

`signal_source` is the first implemented persistent coroutine event source. Its
native callback either detaches and delivers its one `next()` waiter or records one
coalesced pending notification. Ordinary delivery does not release the native
subscription. A completed task stop detaches only the waiter and its cancellation
registration; source close first calls `uv_signal_stop()`, then detaches the
waiter with `UV_ECANCELED`, before starting `uv_close()`.

## Close completion

High-level close state joins repeated waiters around one native close. Native
completion marks the state closed and snapshots waiter delivery before resuming
user code. Storage needed by that callback survives owner release. Listener close
first quiesces its pending accept and provisional child, while connection/UDP close
rejects incompatible active I/O.

See [ownership](ownership-strategy.md), [errors](error-handling-strategy.md), and
[coroutine lifecycle tests](../../tests/test-co-core.cpp).

## Per-wait descriptor readiness

`poll_source` arms one native poll callback for each `next(mask)`. Successful
readiness, native callback error, and cancellation stop native polling, exchange
the waiter slot, unregister cancellation, and resume the detached continuation.
The ordinary delivery path does not access owner or awaiter state after resumption:
user code may immediately rearm or destroy the owner. Close marks the state
terminal before cancelling a waiter, preventing reentrant waits during cleanup.
Unlike `signal_source`, it retains no pending event or active watcher between waits.

## Filesystem event delivery

`fs_event_source` copies the native callback filename before storing or delivering
an event. Copy exceptions are captured inside the C callback and transported to
the waiter or retained pending outcome; both await surfaces rethrow C++ preparation
failures. The shared one-shot slot is exchanged before continuation resumption.
The ordinary callback does not access owner state after delivery. Close sets its
terminal phase before stopping native notifications and cancelling the waiter;
state survives reentrant destruction until native close.


## Raw watcher subscriptions

The private `detail::raw_watcher_handle` shares callback commitment, stop/close
release, and explicit native-query results across `raw::signal`, `raw::poll`,
and `raw::fs_event`, plus timer/idle/prepare/check. It uses existing persistent callable storage; `take()`
extracts a terminal callable and advances its generation before delivery.
No hidden per-operation allocation or coroutine state is introduced.

Prepare paths and runtime callables before native start, commit them only on
success, and retain an old callable after failure only if the source remains
active. Successful static start clears the runtime callable. Stop and close
quiesce before releasing captures; ordinary persistent delivery keeps the local
callable alive and respects generation changes made by reentrant user code.
Poll/fs-event errors stop and extract before delivery. Signal one-shot delivery
uses a persistent native registration stopped before callback invocation, allowing
immediate rearm without native one-shot post-callback stop interfering. This is
single-delivery wrapper behavior, not native signal-handler one-shot semantics.
Native `data` remains application-owned, storage survives close completion, and
callback exceptions still terminate. See [the user contract](../user/raw-watchers.md).


For raw timers, actual native activity at callback entry identifies whether libuv
has rearmed the timer. Inactive one-shot delivery uses terminal extraction;
repeating delivery retains mutable callable state. Stop, close, and one-shot
completion release the subscription, so no-argument `again()` requires native
activity and otherwise returns `UV_EINVAL`; fresh `start` supplies a new callable.
`set_repeat(0)` from a repeated delivery does not erase the already-rearmed final
timeout. A replacement installed by any delivery survives old callback unwinding.

Raw idle/prepare/check starts explicitly stop/restart native dispatch before
committing runtime or static callbacks. Native start alone ignores callback
replacement on active phase watchers on both Unix and Windows. The executing
callable remains local until return, and generation tracking prevents restoration
of a replaced/stopped callback. Peer ordering may change during this restart.


## Raw async callback selection

The async raw wrapper shares `raw_watcher_handle` slot replacement and close/query
machinery. `replace_callback` only moves a prepared runtime callable and advances
the persistent slot generation; unlike `commit_start`, it models no native start.
A fixed native trampoline snapshots a static invoker pointer or invokes the
runtime persistent slot. Static selection clears the stored runtime callable;
runtime selection clears the static pointer. Reentrant replacements survive old
callback unwinding, and an executing callable stays local through return.

All selection/close operations are loop-thread only; cross-thread send touches
neither selection nor `closing()`. Callback argument construction precedes native
initialization or setter entry, so preparation failure leaves the native/current
selection unchanged. Explicit init returns native status and commits its callable
only on success; loop-taking constructors unwrap the result. Repeat init rejects
without touching either callback mode or slot generation, including inside delivery. Close quiesces native
delivery before clearing selection and captures, and raw storage survives native
close completion. No public producer endpoint or concurrent-close guard is added.
See [the user contract](../user/async.md).

Raw signal and fs-event share the explicit-initialization guards used by timer
and loop-phase watchers. The internal initialization helper forwards each family's
notification argument types to the existing raw watcher slot; it adds no callback
storage or dispatch layer. Rejected pre-init starts release their prepared callable
without entering native start or changing signal one-shot bookkeeping.

TCP ordinary/reset close uses a protected stream slot-release hook after native
close has quiesced delivery. Runtime reset close stores its prepared close callable
only after native initiation succeeds, avoiding callback replacement on failure.
Handle read/allocator/listener slots are separate from connect/write/shutdown
request callbacks, which remain retained until their own actual completion.
Raw stream subscription migration is implemented: native success commits runtime/
static selection, read replacement stops before restart, and generations prevent
old callback unwinding from restoring retired state. The allocator and reader are
one paired slot; a checked-out pair spans native buffer allocation through read
notification. A pending pair can survive stop/replacement and is released at read
notification, the next native allocation, or native close completion before user
close delivery. Terminal reads stop/release active ownership before user delivery;
the executing pair still retains its buffer. Allocators must not reconfigure/close
or recursively run the loop while native allocation is in progress. Listener errors
remain persistent while native activity remains; inactive backend failures retire
the selection before delivery. Connect/write/shutdown use separate raw request slots: acceptance commits pending
state, busy requests reject without mutation, and completion extracts selection
and clears pending before user delivery. Runtime/static resubmission shares one
protocol and old completion does not touch the request after delivery. Handle
close retains request state through its own completion.
See [the stream contract](../user/networking/raw-stream.md).

## Raw UDP receive loans and send requests

Raw UDP uses the same generation-tracked persistent slot model as stream reads,
but retains a paired allocator/receiver loan across all native recvmmsg chunks
until FREE. An executing old pair survives stop/replacement/close, while its
generation cannot overwrite a new subscription. The fixed native dispatcher
finishes an already-read batch even after stop from a chunk; no new polling is
started. Close completion releases a remaining loan before close delivery.
`uv::raw::udp_send_request` shares the stream request submission helper, clearing
pending and extracting selection before user completion. Native data remains
application-owned. See [the raw UDP guide](../user/networking/raw-udp.md).
