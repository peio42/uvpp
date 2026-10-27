# V3 architecture

This branch implements the architecture accepted in
[proposal 000](../proposals/000-v3-architecture.md). C++20 and header-only
integration are baseline requirements. The include root is `uvpp`, and the public
namespace is `uv`. The architecture is accepted; the complete API is not yet
implemented or frozen.

## Layers and implementation status

| Layer | Target responsibility | Current implementation |
| --- | --- | --- |
| `uv::raw` | Explicit low-level handles, requests, and operational results | Namespace and error-policy migration remains unfinished; existing wrappers still largely live in `uv` |
| `uv` | Shared vocabulary and stable high-level resource owners | TCP/pipe connections and listeners, UDP sockets, filesystem files, `signal_source`, `fs_event_source`, `poll_source`, loop, addresses, buffers, common results |
| `uv::co` | Cold tasks, spawning, cancellation, and structured composition | `task<T>`, `spawn_handle<T>`, join, cooperative stop, timer sleep, task and resource scopes |
| `uv::ops` | Explicit-result high-level operations and creation | Network listener/socket creation, read/write/accept/receive/send/close, signal/fs-event/poll-source next, DNS resolution, and filesystem open/read/write/close |

Filesystem uses `uv::fs` and `uv::raw::fs`. Raw filesystem submissions return
`uv::status`, with caller-owned requests and explicit cleanup; the callback facade
unwraps submission failures and owns its operation state. Existing process, DNS, watcher,
and utility implementations do not establish their final v3 API merely by
remaining in the source tree.

## One execution context

`uv::loop` is the common owning loop. `uv::loop_view` borrows a native loop;
there is no separate raw or coroutine loop owner. Tasks are cold, bind at root
spawn, and pass their context to awaited child tasks. Native callbacks and
coroutine continuations run on the same loop thread unless an API explicitly
states otherwise. There is no implicit posting queue or second runtime.

The application drives the loop through completion and native close, then calls
`loop.close()`. Loop and resource destructors never run a nested loop. Active
spawn-handle destruction requests stop and releases only public observation; an
internal completion baton retains execution state through actual completion and
reclaims the root only after final suspension. An unobserved root failure is routed
exactly once through the loop failure handler; its default policy terminates.

## Native identity and ownership

Low-level wrappers use composition and stable native storage rather than inheriting
from libuv structs. They are not copyable or movable after initialization.
Callback reconstruction must respect the actual storage layout and type; native
handles belonging to high-level owners are not low-level wrapper instances.

High-level owners move by transferring stable storage. Close state retains that
storage through the native close callback. Borrowed views do not retain resource
ownership. Native access is explicit. Raw wrapper `data` remains application-owned;
high-level owners and internal operations may reserve it for implementation state.
Prefer it when it avoids an artificial dependency on object layout; the choice
depends on the lifetime and simplest reconstruction mechanism, not just the layer.
See [ownership](ownership-strategy.md) and [callback strategy](callback-strategy.md).

High-level operations own their control state, but writes/sends borrow payloads.
Cancellation does not complete work or release borrowed bytes. Terminal delivery
releases operation slots before resuming user code. Current owners permit one
operation per direction, including duplex I/O; see [I/O concurrency](io-concurrency.md).

`signal_source` owns one persistent native signal subscription and one exclusive
coroutine consumer slot. Ordinary signal delivery keeps the subscription active;
completed task cancellation releases only that consumer slot and leaves the source
usable. Terminal source close stops native delivery before releasing its waiter.
One notification may be retained while no waiter is active; it is intentionally
coalesced rather than becoming an unbounded queue.

## Errors and costs

The v3 target uses explicit operational results in raw APIs, throwing high-level
awaits, and explicit-result `uv::ops` awaits. Existing low-level throwing submission
helpers remain a migration gap, not the rule for new raw APIs. Allocation/setup
failures require separate contracts; status-oriented does not imply `noexcept`.

Raw primitives must not gain hidden operation allocations. Existing callback,
string, and native-lifetime storage costs must be documented. High-level owners,
scopes, and coroutine frames may allocate. Allocation-free claims require measured
paths; [proposal 010](../proposals/010-validation-and-performance.md) tracks gates.

## Repository organization

- `include/uvpp/core/`: loop, native storage, buffers, callback boundary, results, capabilities.
- `include/uvpp/handles/` and `requests/`: current low-level binding machinery.
- `include/uvpp/net/`: address vocabulary and network owners.
- `include/uvpp/co/`: tasks, cancellation, sleep, task scopes, resource scopes.
- `include/uvpp/detail/`: shared stream I/O and asynchronous close machinery.
- `docs/user/`: strictly v3 usage and current availability.
- `docs/design/`: implementation contracts and contributor rules.
- `docs/proposals/`: unfinished contracts, decisions, and validation work.

The [design index](index.md) links implementation and test evidence. A proposal
sketch is not callable API; keep code, implemented documentation, and proposal
progress synchronized as each slice lands.

## Per-wait descriptor readiness

`poll_source` owns stable `uv_poll_t` storage while borrowing its descriptor.
Construction leaves polling inactive; `next(mask)` arms one exclusive native wait.
Success, completion error, and cancellation stop polling and release the waiter
before resuming user code. A later wait can select a new mask; no readiness is
queued between waits. Close marks the source terminal before cancelling its
waiter and retains storage through native close. This intentionally differs from
the persistent `signal_source` subscription.

## Filesystem event subscriptions

`fs_event_source` owns stable native watcher storage and reserves `data` for its
state. It starts one persistent subscription at construction and borrows the
loop, without owning the watched filesystem object. Its exclusive waiter uses
`one_shot_callback_slot`; ordinary delivery and cancellation release that waiter
before resumption while retaining the subscription. Terminal close quiesces the
watcher, cancels its waiter, clears retained outcomes, and joins native close.
Reentrant owner release during delivery is safe through close completion.

One pending outcome bounds notification retention: successes merge flags and
retain a filename only while all names agree; a failure replaces success and
remains sticky until consumed. Callback filename materialization exceptions travel
as `exception_ptr` and rethrow at the await in either policy. See the
[user contract](../user/fs-events.md) for costs and native portability limits.
The raw wrappers `uv::raw::{signal,poll,fs_event}` use explicit start/stop status
and query results. Shared subscription bookkeeping commits callbacks after
successful start, preserves an old callable only while its native source remains
active, and releases terminal slots before user delivery. High-level source
ownership and pending-event policies remain separate. Raw signal, poll, and fs-event
also support default construction and status-returning `init(loop/loop_view)`
through the shared `initializable_raw_watcher_handle`, now parameterized by
notification payloads. Native `loop` tracks initialization without added state;
pre-init operations (including fs-event `path`) return `UV_EBADF`, observations
are false, and close/ref operations are inert. Repeat init reports `UV_EALREADY`
through and after close. Signal native init failure precedes handle registration
and leaves the sentinel unset, allowing retry without handle close; any retained
loop infrastructure belongs to the loop. See [raw watchers](../user/raw-watchers.md).


## Raw timers and loop phases

`uv::raw::{timer,idle,prepare,check}` use the same `raw_watcher_handle` subscription
bookkeeping as the migrated signal/poll/fs-event families. All four additionally
support default construction and explicit `init(loop/loop_view) -> status`; their
loop-taking constructors unwrap that status. The shared internal
`initializable_raw_watcher_handle` factors out pre-init observation, close/ref,
and native-query guards without adding storage or changing callback ownership.
The native `loop` pointer is the initialization sentinel, with no additional state
field. It remains set after close, preventing reinitialization. All four guard operations before initialization;
Async follows the same initialization lifecycle, with immediate activation as
documented below. Start/stop
and timer `again`/`set_repeat` return `status`, with shared explicit native-query
results. Terminal one-shot timer delivery extracts its callable before invocation,
while repeated events retain the callable.
Inactive `again` cannot resurrect a callback released by stop or completion.
Phase starts stop/restart native dispatch so active runtime/static replacement
actually installs the new callback. Duration validation precedes native mutation,
and timer getters preserve unsigned native milliseconds. Shared loop, native
address stability, application-owned `data`, and explicit close completion are
unchanged. See [raw callback watchers](../user/raw-watchers.md).


## Raw async notifications

`uv::raw::async` extends the shared raw watcher callback/close/query bookkeeping
with a fixed native trampoline and one non-owning static dispatch pointer.
Default construction leaves it uninitialized. Explicit `init(loop/loop_view)`
supports empty/runtime/static callback forms, returning native status; loop-taking
constructors unwrap the same result. The shared initialization helper guards
observations, close/ref, and native queries without added storage. Initialization
activates the handle immediately; there is no start/stop facade.
`send() noexcept -> status` directly adapts `uv_async_send` without reading wrapper
callback or closing state. It rejects an unset native loop sentinel with
`UV_EBADF`; after publication that pointer is immutable. Init must complete before
producer publication and must never overlap sends. The caller prevents new sends and joins/synchronizes
in-flight producers before close. Callback setters are loop-thread operations
returning `status`, reject pre-init/closing state with `UV_EBADF`, and preserve reentrant
replacement through generation tracking. Empty runtime selection drops delivery
without stopping the handle. Repeat init, including during/after close, reports
`UV_EALREADY` without changing dispatch or pending notifications. Callable
preparation can throw before init entry; moves after native success do not throw.
Unix native failures precede handle registration and require no wrapper close. Native `data`, address stability, explicit close,
and shared loop rules are unchanged. `loop_posting` keeps its independent native
storage, mutex admission, queue, and producer endpoints. See
[raw async notifications](../user/async.md).

## Raw poll initialization rollback

Raw poll synchronously rolls back an inactive registration if native Windows
initialization fails after registration but before any request or poll-owned
resource exists. This preserves immediately destructible/retryable failed init
and throwing constructors without driving the loop or adding cleanup allocation.
It requires an isolated dependency on libuv's private registration queue.

The [rollback audit](raw-poll-init-rollback.md) records public-API alternatives,
exact official tags reviewed, both queue layouts, backend/failure prerequisites,
and the native Windows Winsock fault-injection tests and pinned CI jobs.

## Raw TCP handle migration

`uv::raw::tcp` combines the `uv::raw::stream` storage/submission substrate
with `detail::initializable_handle` lifecycle/native-query guards. The watcher
initialization helper now specializes this same storage-free base; it preserves
its prior callback/close semantics. TCP native init failures are already unlinked
and resource-cleaned by libuv (Unix/Windows init_ex source reviewed at 1.45.0,
1.51.0, and 1.53.0); clearing native storage restores the stale loop sentinel and
preserves application `data`, without a second queue rollback.

TCP init/open/bind/options/reset initiation return status; address queries return
`result<socket_address>`, descriptor queries return `result<uv_os_fd_t>`, and buffer
queries retain counts. Keepalive takes chrono seconds with native-range validation.
C++ address/callback preparation remains separate from native operational errors.
Normal close is void, while reset close can fail before initiation. Reset runtime
callback installation is committed only after native success; failure retains
existing stream slots and releases the rejected close callback.

The stream substrate is now `uv::raw::stream`, also consumed by `uv::raw::pipe` and the still-legacy
TTY family wrapper. Listen/read start/stop/accept/blocking use statuses;
read outcomes keep signed counts, EOF and borrowed buffers. Runtime/static
subscription selection is committed after successful native start. Read replacement
stops the old source first: restart failure after successful stop leaves it stopped.
The paired allocation/read slot supports generation-tagged checkout across one
native buffer loan, with inline active/pending storage and no callback copies.
Terminal read stops before user delivery. Listener errors retain the
persistent listener while native activity remains; backend-stopped errors release
selection before delivery (notably Windows fatal accept failures).

Normal/reset close releases active subscriptions after native initiation, while an
outstanding buffer loan retains its original allocator/reader through native read
or close completion. All close forms install internal completion cleanup before
user delivery, even without a user close callback. Reset failure leaves existing
subscriptions untouched. Request callbacks/storage remain independent. Connect,
write and shutdown now use separate `uv::raw` requests and status-oriented
submission. A shared inline request state rejects pending reuse, commits callback
selection only after native acceptance and clears pending/selection before user
completion, with no request access afterwards. Close does not retire request
state early. Pipe-specific connect/IPC entry points now return statuses over
this shared lifecycle.
See [raw TCP](../user/networking/raw-tcp.md) and
[stream subscriptions](../user/networking/raw-stream.md).

## Raw pipe migration

`uv::raw::pipe` combines the initializable-handle guards with the raw stream
substrate, without a legacy alias or changes to high-level owner storage. Native
Unix/Windows pipe init has no fallible post-registration step, so it needs no poll
rollback or extra lifetime allocation. Bind/open/chmod/connect/IPC submissions
return statuses. Pending-instance configuration adapts native void behavior;
pending counts/types remain native observations. Name queries return
`result<string>`, with separate C++ allocation and exact native byte lengths,
including Linux abstract-name NUL bytes. Legacy terminated-name copies remain
fallible C++ preparation; modern length-aware APIs retain capability gates.
IPC runtime/static submissions use the shared request protocol and typed immediate
writes retain partial/would-block outcomes. Exported native storage/payload remains
borrowed through completion. See [the raw pipe contract](../user/networking/raw-pipe.md).

## Raw UDP socket migration

`uv::raw::udp` now uses the common initializable-handle guards over its existing
inline native storage. Init/default/eager family/recvmmsg forms share status
initialization and throwing constructor unwrapping. Unix fails eager socket
creation before registration. Reviewed Windows 1.45/1.48/1.53 native paths remove
their failed registration and close newly created sockets; uvpp clears native
storage, preserving application data and resetting the stale loop sentinel without
a second unlink or native close. No added native-lifetime allocation is needed.
Socket operations return statuses and address/descriptor/buffer queries retain
results. Multicast string copies keep C++ exceptions separate from native errors;
embedded NUL inputs are rejected before submission. Numeric/bool options have
native status/noexcept contracts. Duplicate close is inert. Receive subscriptions
now use generation-tracked paired callback loans, retained from allocation through
native delivery, including recvmmsg chunks/FREE actually delivered by libuv.
Stop/replacement/close preserve native callback state without rewriting fields or
forcing FREE delivery. A pending pair survives until native FREE, the next
allocation or close completion; a new pair applies to later allocations. Terminal receive errors stop the
source before user delivery. Close completion releases any remaining loan.
`uv::raw::udp_send_request` shares the stream request submission helper: pending
guards, commit after native acceptance, and extraction before completion delivery.
C++ callable preparation remains separate from native errors. Immediate sends and
suffix batches validate metadata and preserve byte/datagram progress and would-block
without allocating hidden C++ metadata. Typed receive/immediate/batch values stay
in `uv`. High-level socket storage is unchanged. See
[the raw UDP contract](../user/networking/raw-udp.md).
