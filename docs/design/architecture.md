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
ownership and pending-event policies remain separate; see [raw watchers](../user/raw-watchers.md).


## Raw timers and loop phases

`uv::raw::{timer,idle,prepare,check}` use the same `raw_watcher_handle` subscription
bookkeeping as the migrated signal/poll/fs-event families. All four additionally
support default construction and explicit `init(loop/loop_view) -> status`; their
loop-taking constructors unwrap that status. The shared internal
`initializable_raw_watcher_handle` factors out pre-init observation, close/ref,
and native-query guards without adding storage or changing callback ownership.
The native `loop` pointer is the initialization sentinel, with no additional state
field. It remains set after close, preventing reinitialization. All four guard operations before initialization;
signal/poll/fs-event/async retain their existing constructor setup. Start/stop
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
Initialization activates the handle immediately; there is no start/stop facade.
`send() noexcept -> status` directly adapts `uv_async_send` without reading wrapper
callback or closing state. The caller prevents new sends and joins/synchronizes
in-flight producers before close. Callback setters are loop-thread operations
returning `status`, reject closing state with `UV_EBADF`, and preserve reentrant
replacement through generation tracking. Empty runtime selection drops delivery
without stopping the handle. Construction/preparation retain their separately
documented exception policy. Native `data`, address stability, explicit close,
and shared loop rules are unchanged. `loop_posting` keeps its independent native
storage, mutex admission, queue, and producer endpoints. See
[raw async notifications](../user/async.md).
