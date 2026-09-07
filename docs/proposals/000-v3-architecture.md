# V3 Architecture

Status: accepted.

Target: v3. This umbrella proposal defines the direction for the other v3
proposals. It does not describe the implemented v2 API or authorize changing v2
policy piecemeal. Specialized proposals refine these invariants; incompatible
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
Filesystem raw APIs move conceptually from `uv::fs::raw` to `uv::raw::fs`.

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
primitive. Generic public coroutine close is a separate decision. A lexical C++
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
`std::chrono` durations, and application-owned native `data` fields. Raw primitives
must not acquire hidden operation allocations, locks, or coroutine state. Existing
explicit storage costs and justified native-lifetime allocations must be documented;
zero-allocation claims apply to measured paths, not all wrappers indiscriminately.
High-level owners and coroutine frames may allocate, with costs made visible.

Do not require coroutines for callback applications, a general executor framework,
universal reference counting, or a second I/O object hierarchy. Optimization follows
measurement rather than compatibility aliases or speculative abstraction layers.

## Dependencies and delivery gates

1. Establish this architecture (000), including namespaces and ownership/error
   boundaries, before broad API implementation.
2. Establish validation and performance baselines (010); compatible work can land
   in v2. This is a baseline gate, not completion of every future benchmark.
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
against equivalent raw paths and supply v2-to-v3 migration examples. Compatibility
aliases are not required and must not dictate the new model.

## Implementation and documentation lifecycle

The architectural direction is accepted, but the three-layer v3 API is largely not
implemented. The experimental `uv::co::task<T>`, root `spawn`, child-task await,
timer `sleep_for`, and `uv::tcp_connection` connect/close slices are the first
implementations; they intentionally do not settle task scopes, cancellation,
asynchronous join, error-policy pairs, or owner APIs.
Current v2 behavior remains authoritative in `docs/design/` and `docs/user/`
except where explicitly marked experimental.

As work lands, update those documents and retain only remaining work and necessary
dependencies here. Remove this proposal when its architecture is implemented and
its remaining details are covered by current documentation or active specialized
proposals. Follow the [proposal lifecycle](index.md#proposal-lifecycle): Git retains
history; an ADR may preserve durable rationale when needed.
