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
| `uv` | Shared vocabulary and stable high-level resource owners | TCP/pipe connections and listeners, UDP sockets, filesystem files, `signal_source`, loop, addresses, buffers, common results |
| `uv::co` | Cold tasks, spawning, cancellation, and structured composition | `task<T>`, `spawn_handle<T>`, join, cooperative stop, timer sleep, task and resource scopes |
| `uv::ops` | Explicit-result operations on the same owners | Owner close, signal-source next, DNS resolution, and filesystem open/read/write/close |

Filesystem targets `uv::fs` and `uv::raw::fs`; existing `uv::fs::raw` code has
not yet completed this migration. Existing filesystem, process, DNS, watcher,
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
reclaims the root only after final suspension. Unobserved root failures currently
terminate after active-handle abandonment; configurable routing remains unfinished.

## Native identity and ownership

Low-level wrappers use composition and stable native storage rather than inheriting
from libuv structs. They are not copyable or movable after initialization.
Callback reconstruction must respect the actual storage layout and type; native
handles belonging to high-level owners are not low-level wrapper instances.

High-level owners move by transferring stable storage. Close state retains that
storage through the native close callback. Borrowed views do not retain resource
ownership. Native access is explicit, and native `data` remains application-owned.
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

- `include/uvpp/core/`: loop, native storage, callback boundary, results, capabilities.
- `include/uvpp/handles/` and `requests/`: current low-level binding machinery.
- `include/uvpp/net/`: address/buffer vocabulary and network owners.
- `include/uvpp/co/`: tasks, cancellation, sleep, task scopes, resource scopes.
- `include/uvpp/detail/`: shared stream I/O and asynchronous close machinery.
- `docs/user/`: strictly v3 usage and current availability.
- `docs/design/`: implementation contracts and contributor rules.
- `docs/proposals/`: unfinished contracts, decisions, and validation work.

The [design index](index.md) links implementation and test evidence. A proposal
sketch is not callable API; keep code, implemented documentation, and proposal
progress synchronized as each slice lands.
