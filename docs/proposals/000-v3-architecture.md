# V3 Architecture

Status: accepted.

Target: v3. This umbrella proposal defines the direction for the other v3
proposals. It does not claim that the complete target API is implemented. Specialized proposals refine these invariants; incompatible
alternatives require an explicit revision here.

## Motivation

Make abstraction level, ownership, and error policy visible at the call site.
Source compatibility with v2 is secondary to a coherent API and the library's
lifetime and cost guarantees. Preserve libuv interoperability and callback use
while making safe ownership and coroutine composition accessible by default.

## Architectural invariant

uvpp v3 consists conceptually of three interoperable layers:

1. **Raw layer:** thin C++ mapping of libuv, explicit ownership and lifetime,
   caller-controlled requests and buffers. Namespace `uv::raw`.
2. **High-level layer:** safe, ergonomic C++ ownership and result types.
   Namespace `uv`. May internally use raw primitives.
3. **Coroutine layer:** composable asynchronous frontend over high-level or
   internal operations. Coroutine primitives live in `uv::co`. This layer does
   not duplicate the libuv binding or introduce another hierarchy of I/O types.

`uv::loop` is the common event-loop abstraction shared by all layers. Moving
between API layers must not require a second event loop, executor, scheduler,
or hidden runtime.

## Namespace responsibilities

| Namespace | Proposed responsibility |
| --- | --- |
| `uv` | Common vocabulary and recommended high-level objects and conveniences |
| `uv::raw` | Explicit low-level handles, requests, and native operations |
| `uv::fs` | High-level filesystem API |
| `uv::raw::fs` | Caller-controlled filesystem request and cleanup protocol |
| `uv::ops` | Explicit-result operations on high-level objects; no separate owners |
| `uv::co` | Tasks, task scopes, spawning, and coroutine composition |

`uv::ops` is an operation surface, not a fourth abstraction layer. Choosing
explicit results must not force callers to manage native requests themselves.
Domain subdivisions such as `uv::ops::fs` may organize operations consistently.
Shared loop, buffer, address, and result vocabulary must not be duplicated merely
because several layers use it.

The existing low-level TCP wrapper would become `uv::raw::tcp`. High-level roles
should be explicit, for example `uv::tcp_listener` and `uv::tcp_connection`.
These names are provisional; there should be no corresponding `uv::co::tcp`.
Filesystem raw APIs now live in `uv::raw::fs`: requests, completion results,
directories, flags, and callback/static operations migrate together. Submission
returns `uv::status`; native request cleanup remains explicit.

## Common loop and execution

There is one canonical public owning loop type, `uv::loop`. A borrowed loop view
expresses borrowing of the same execution context, not a separate runtime. Do
not introduce another owning `uv::raw::loop`. Raw callbacks, high-level operations,
and coroutine tasks can coexist on one loop with no implicit thread hop.

Tasks are cold and have no execution context at construction. `spawn(loop, task)`
consumes a root task and binds that context; nested awaited cold tasks are consumed
and inherit the parent's context. Tasks start only once. Spawn handles own root executions and provide
asynchronous join; cross-loop joining is unsupported initially. Active-handle
destruction requests cancellation and retains execution state through actual
completion and cleanup. Start with task, spawn handle, and task scope; public
detach is deferred. [001](001-coroutines.md) and [003](003-cancellation-and-task-scopes.md)
refine these contracts.

The application controls loop driving and shutdown. Owners and tasks borrow the
loop, which must survive and be driven through all pending native cleanup.
Destructors must not run a nested loop to emulate synchronous cleanup.

Continuation placement, inline completion, fairness, and shutdown need explicit
contracts in [007](007-loop-scheduling.md). An optional posting component may
provide synchronized cross-thread publication on the same loop. It is not a
prerequisite for ordinary loop-thread coroutine use or inter-layer access. Any
queue, wakeup handle, or retained scheduling state must have documented ownership,
allocation, and loop-liveness costs. Cross-thread entry points remain explicit.

## Ownership boundaries

Raw handles and native storage remain address-stable, non-copyable, and
non-movable after initialization. Raw handle destructors do not silently start
asynchronous close. Caller-owned requests, borrowed buffers, and explicit native
cleanup remain supported and documented.

High-level owners may move by transferring ownership of stable storage. Moving
an owner must never relocate the native handle or invalidate pending callbacks.
Operation-state ownership does not imply payload ownership. Borrowing is the
default payload policy for asynchronous write/send operations; copying and
ownership transfer use explicit semantic names or types. Borrowed bytes must stay
alive, address-stable, and unmodified until actual completion, including after a
cancellation request. Retaining a task frame does not retain external borrowed data.

Adoption transfers a genuinely transferable owner, not an arbitrary reference to
an initialized wrapper. A raw handle on the stack cannot be adopted into an owner
that may outlive that stack storage. Borrowing remains a distinct contract. No
implicit adoption or concurrent independent close owners are permitted.

Explicit raw/native access does not transfer ownership. Each high-level type must
define which native operations preserve its invariants, including callback-slot
exclusivity and pending operations. A callback-scoped raw accessor alone cannot
prevent an invalid close, retained pointer, or conflicting subscription.

High-level cleanup must cover exceptional exits and retain storage through native
completion. [002](002-async-ownership.md) defines explicit asynchronous exit and
the fallback contract for destruction without it. No layer may equate requested
cancellation with completed work or permission to release buffers.

Ownership and resource scopes require an awaitable internal close-completion
primitive. Experimental high-level owners expose the corresponding public `close()`
operation under the contract in [002](002-async-ownership.md). A lexical C++
destructor cannot join asynchronous cleanup; an asynchronous scope boundary must
provide that guarantee. Terminal operations and subscriptions release callback-slot
ownership before delivering completion to user code, after quiescing their native
source; state still needed by in-flight callbacks remains alive. See [004](004-operation-state.md).

## Error and API layering

Use a structural distinction rather than a family of error-policy suffixes:

```cpp
// Proposed naming principle, not complete signatures:
uv::foo(...);       // ergonomic / throwing surface
uv::ops::foo(...);  // explicit operation / status-oriented surface
```

Equivalent domain or member forms may be used without adding `_status`, `_ec`,
`try_`, or `async_` solely to select error handling. Semantic distinctions such
as default-borrowing `write`, explicit `write_copy`, and immediate `*_now`
operations remain useful.
Reserve `try_*` for actual attempt-style semantics such as `try_lock` in v3.

Raw native operations use explicit status/results for operational failures.
Construction, callback storage, and allocation failures need separately specified
contracts; this direction does not claim that every raw API is `noexcept`.

High-level throwing and explicit-result surfaces share submission, completion,
and cleanup machinery. An awaited explicit operation reports both native
submission and completion errors as results. A throwing await reports failures
at the await expression. Preparation must not bypass the chosen policy.

Callback APIs may throw on immediate submission failure in their ergonomic form;
later completion is delivered as a result, never by throwing back to the original
caller. Exceptions must not cross libuv C callbacks. User callback exceptions,
unobserved task failures, allocation failures, and programmer misuse are distinct
from native operational failures; [006](006-errors-and-results.md) defines their
boundaries. EOF, would-block, and partial progress remain explicit domain outcomes.

Status-oriented does not automatically mean `noexcept` or support for compiler
exception disabling. No full non-throwing guarantee is made until setup and result
materialization failures have a specified channel.

## Shared implementation and costs

[004](004-operation-state.md) defines narrow shared operation protocols for
callback and coroutine delivery. Neither frontend should reproduce the binding,
cleanup rules, or native ownership machinery. This does not require a universal
virtual operation hierarchy or routing coroutines through `std::function`.

Keep C++20 and header-only integration, focused includes, concepts for structural
public template contracts, explicit native access and `.view()` borrowing,
`std::chrono` durations, and distinct raw/high-level native `data` rules. `uv::raw`
wrapper fields remain application-owned; high-level owners and internal operation
state may reserve them as implementation storage and should do so when it avoids
an artificial dependency on object layout. Persistent high-level owners preferably
point `data` at stable state. Internal one-shot operations may use an awaiter/state
pointer or `native_storage`, whichever is simpler without unnecessary constraints;
using `data` is not a layer-wide requirement. Raw primitives must not acquire
hidden operation allocations, locks, or coroutine state. Existing
explicit storage costs and justified native-lifetime allocations must be documented;
zero-allocation claims apply to measured paths, not all wrappers indiscriminately.
High-level owners and coroutine frames may allocate, with costs made visible.

Do not require coroutines for callback applications, a general executor framework,
universal reference counting, or a second I/O object hierarchy. Optimization follows
measurement rather than compatibility aliases or speculative abstraction layers.

## Dependencies and delivery gates

1. Establish this architecture (000), including namespaces and ownership/error
   boundaries, before broad API implementation.
2. Establish validation and performance baselines (010); this is a baseline gate, not completion of every future benchmark.
3. Define initial contracts for ownership (002), cancellation (003), operation
   state (004), errors (006), and scheduling (007). Fix minimum buffer borrowing
   and transfer rules from (005) at this stage.
4. Prototype coroutines (001) against these contracts. Feed findings back into
   the contracts before freezing signatures; do not require all foundations to
   be fully implemented before experimentation.
5. Validate buffer adapters and bounded flow control (005) with realistic network
   and filesystem workflows, including exceptional cleanup and slow consumers.
6. Freeze the public API only after lifecycle, interop, error, and cost validation.

Move-only callbacks (008) and diagnostics (009) are transversal work. They become
gates where a selected public contract depends on them; otherwise they can advance
independently. Proposal numbers are stable identifiers, not implementation order.

## Open decisions and validation before API freeze

Detailed proposals must settle operation initiation, result shapes, callback and
awaitable spellings, allocation-failure channels, owner factories/adoption,
scope-exit behavior, continuation fairness, and supported platform/libuv baselines.
These questions must not reopen the common loop or create duplicate I/O bindings
without revising this proposal explicitly.

Acceptance fixes the architectural direction; it does not claim that its APIs are
available or that every lifecycle and performance gate has passed. Before freezing
the public v3 API, demonstrate raw callbacks,
high-level callbacks, and coroutine I/O on the same loop; paired throwing and
explicit-result failures; owner moves with stable native addresses; exceptional
close and cancellation; and explicit buffer lifetimes. Record allocation costs
against equivalent raw paths and supply complete v3 usage examples. Compatibility
aliases are not required and must not dictate the new model.

## Implementation and documentation lifecycle

The architectural direction is accepted. Experimental implementation now includes
`task<T>`, `spawn_handle<T>`, child awaits, join and cooperative stop, timer sleep,
task scopes, TCP/pipe connection and listener owners, UDP sockets, paired synchronous
listener/socket factories under `uv::ops`, awaited close
with `uv::ops::close`, and resource scopes for these network families.

The raw namespace/error-policy migration, complete paired operation surfaces,
active spawn-handle retention fallback, generic resource ownership, and broader
validation remain unfinished. Specialized proposals retain their open decisions.

The process slice now provides `uv::process`: synchronous `uv_spawn()`
construction, one cancellable `wait()` consumer, retained terminal exit result,
immediate `kill()`, and close only after exit. Its historical callback wrapper is
available as `uv::raw::process`. Stdio, process supervision, and resource-scope
adoption remain outside this slice.

The README and `docs/user/` now teach v3 exclusively, including explicit availability
and limitations; superseded user guides are removed rather than archived.
`docs/design/` records implemented contracts, contributor rules, and remaining
migration boundaries. This documentation review does not make unimplemented
proposal signatures available or freeze the experimental API.

As work lands, update those documents and retain only remaining work and necessary
dependencies here. Remove this proposal when its architecture is implemented and
its remaining details are covered by current documentation or active specialized
proposals. Follow the [proposal lifecycle](index.md#proposal-lifecycle): Git retains
history; an ADR may preserve durable rationale when needed.

## Poll-source implementation progress

The experimental `uv::poll_source` owns stable native poll storage and borrows
its descriptor. It provides per-wait masks, paired throwing/result waits, and
terminal asynchronous close. Persistent subscriptions and scope adoption remain
deferred; see the [implemented contract](../user/poll.md).

## Filesystem-event implementation progress

The experimental `uv::fs_event_source` now provides a persistent native
subscription, one exclusive cancellable `next()` waiter, one coalesced pending
outcome, and terminal non-cancellable close. `uv::ops::next/close` share the same
state and lifetime protocol. Filename values are owned; missing or ambiguous
coalesced names are absent. Native callback failures remain observable until
consumed; C++ materialization exceptions rethrow at the await in both policies.
The callback wrapper is now `uv::raw::fs_event`, with explicit raw start/stop
status and path query results. See the [implemented contract](../user/fs-events.md).
Resource-scope adoption, lossless queues, and broader platform/cost validation
remain outside this slice.


## Raw watcher migration progress

Signal and poll callback wrappers now join fs-event in `uv::raw`; common signal
numbers, poll masks, and socket-poll tags remain in `uv`. Their start/stop and
native-query errors use explicit status/results. Callback rollback, successful
stop/close release, terminal poll/fs-event error delivery, and signal one-shot
quiescence are implemented without changing high-level source contracts. See
[raw watchers](../user/raw-watchers.md). Fs-poll and other raw families remain
separate migration work; broader platform/performance gates remain open.


## Raw timer and loop-phase migration progress

`uv::raw::{timer,idle,prepare,check}` now share migrated raw watcher storage,
explicit start/stop status and native queries, successful replacement commitment,
and capture release on stop/close. Timer one-shot completion extracts its callable
before user delivery; repeated callbacks retain mutable state. Timer `again`
requires an active subscription, and `set_repeat` reports duration/closing errors
as status. Raw duration validation and unsigned chrono getters are documented.
Idle/prepare/check restart native delivery to support active runtime/static
replacement. The shared loop, high-level coroutine sleep, and application-owned
raw `data` contracts remain unchanged. See [the implemented user contract](../user/raw-watchers.md#timers)
and [lifecycle tests](../../tests/test-raw-loop-watchers.cpp). Broader platform,
allocation-failure, and performance validation remain open; this is not an API
freeze or a high-level owner expansion.


## Raw async migration progress

The caller-owned wrapper is now `uv::raw::async`, with explicit `send` and callback
setter status/results. Its fixed native trampoline supports runtime/static
replacement, including reentrant replacement, without native reinitialization.
It shares raw callback/close/query machinery, adds one static function pointer,
and preserves application-owned native `data` and stable storage. Initialization
activates the handle immediately. Explicit init returns status and loop-taking
constructors unwrap it; no start/stop API, queue, lock,
or producer endpoint is added. Only send is cross-thread safe. The caller must
prevent new sends and synchronize every in-flight send before close; send does not
read loop-thread closing state or promise a recoverable after-close error.
High-level `loop_posting` retains its independent synchronized native protocol.
See [the implemented user contract](../user/async.md) and
[raw async tests](../../tests/test-raw-async.cpp). Broader native platform,
allocation-failure, and performance gates remain open.

### Raw timer explicit initialization

Raw timer supports default construction, a native-loop-pointer `initialized()`
observation, and explicit status-returning `init(loop&/loop_view)` alongside the
throwing constructors. Before-init operations are guarded without additional
state storage; successful initialization cannot be repeated, including after
close. Signal/poll/fs-event/idle/prepare/check also implement this lifecycle through
shared internal guards; their start/stop and callback replacement protocol remains unchanged.
Other raw families retain constructor initialization pending their own migration. See [the implemented timer contract](../user/raw-watchers.md#explicit-initialization)
and [error-policy progress](006-errors-and-results.md#raw-timer-explicit-initialization-progress).

Raw poll descriptor/socket initialization also supports explicit status and rolls
back incomplete native registration before reporting failure. Windows libuv's late
init error branch requires an isolated internal queue rollback; see
[implemented architecture](../design/architecture.md#raw-poll-initialization-rollback).
Native external nonblocking side effects are not rolled back. Broader native
backend failure injection remains a validation gate.

## Raw TCP first migration slice

The caller-owned TCP class is now `uv::raw::tcp` without a legacy alias. Default
construction, explicit status init and throwing constructors share native-loop
initialization tracking. Native TCP init_ex failures are already unregistered and
resource-cleaned by libuv; uvpp clears the stale loop pointer while preserving
application data and address stability. No poll-specific queue rollback is reused.
Open/bind/options/reset return status, address/descriptor/buffer queries retain
typed payloads, and keepalive duration uses chrono seconds with range validation.
Normal/reset close releases handle stream slots only after native quiescence;
reset failure retains existing subscriptions and does not install its new close
callback. Request callbacks/storage remain independent until actual completion.

The substrate is now `uv::raw::stream`. Persistent listen/read start/stop/accept/
blocking operations use statuses and support runtime/static subscription selection.
Read allocation/delivery share an inline generation-tagged pair, preserving the
original buffer owner and reader across replacement/stop through native completion.
Terminal reads stop/release active slots before delivery. All normal/reset close
forms retire pending loans at native completion before user close callbacks.
Connect/write/shutdown request migration is implemented in the progress section
below, followed by the implemented pipe slice. UDP socket operations and send/receive/request protocols are implemented; see the raw UDP guide.
High-level network owners remain independent. See
[the stream contract](../user/networking/raw-stream.md) and
[stream validation](../../tests/test-raw-stream.cpp).

## Raw stream/TCP request progress

Connect/write/shutdown requests now live separately in `uv::raw`, with immovable
application-owned storage and `pending()` state. Stream write/shutdown and TCP
connect return submission statuses. Pending reuse is `UV_EBUSY`; native rejection
retains no new callback and produces no completion. Runtime/static selection is
extracted and pending cleared before delivery, allowing reentrant reuse without
old completion clobbering it. Handle close retains request/payload lifetimes until
native completion. Typed immediate write/read outcomes remain distinct. Pipe-specific connect/IPC interfaces now return statuses over the shared request
state; see the pipe progress section below. See the
[implemented request contract](../user/networking/raw-requests.md) and
[real request tests](../../tests/test-raw-stream-requests.cpp).

## Raw pipe progress

The caller-owned pipe wrapper is now `uv::raw::pipe`, with explicit status init,
throwing loop constructors, native-loop initialization tracking, common stream
close cleanup, status bind/open/configuration/connect and runtime/static IPC
submission. Name queries retain string payloads as results and preserve native
binary-name lengths; C++ allocations and legacy name copies retain exception
contracts. Counts/types and immediate write progress remain typed observations.
Raw request pending/reuse/completion and borrowed payload/exported-handle lifetimes
are shared with stream/TCP. High-level pipe owners are unchanged. The [raw UDP migration](../user/networking/raw-udp.md), including send/receive/request protocols, is now implemented. See [implemented pipe behavior](../user/networking/raw-pipe.md)
and [raw pipe tests](../../tests/test-raw-pipe.cpp).

## Raw UDP socket slice

Initialization and socket operations now live on `uv::raw::udp`: default/status
init and throwing constructors, open/bind/peer association, native options,
multicast/source membership and typed address queries. Native failure leaves
retryable init storage with application data preserved; Windows native cleanup
already removes late-failure registrations, so uvpp only resets the stale sentinel.
Multicast C++ string allocation remains throwing independently of native statuses.
Send/receive/request migration is also implemented: receive pairs survive native
buffer loans including recvmmsg FREE, and separate raw send requests share the
pending guard and pre-delivery release protocol. Immediate/suffix batch results
retain native progress, would-block and errors; C++ preparation stays separate. See
[implemented socket behavior](../user/networking/raw-udp.md) and
[UDP socket tests](../../tests/test-raw-udp.cpp).

## Raw UDP I/O progress

The [implemented raw UDP contract](../user/networking/raw-udp.md) now covers
status receive start/stop and asynchronous send submission, separate
`uv::raw::udp_send_request`, runtime/static callback replacement/reuse and close.
Paired subscription loans preserve allocator-owned storage through native delivery
and recvmmsg chunks/FREE, including stop/replacement/close from a chunk. Native
receive errors quiesce before delivery. Immediate sends and suffix batches preserve
byte/datagram progress and would-block without hidden C++ operation metadata.
See [real native tests](../../tests/test-raw-udp-io.cpp) and the
[isolated allocation tests](../../tests/udp-allocation.cpp). Broader proposal work
remains open; high-level UDP owners and their operation state are unchanged.
