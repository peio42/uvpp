# V3 consistency registry

This registry makes migration gaps and family-specific contracts visible in one
place. **Implemented** means callable and documented on this branch; **target**
means accepted direction but incomplete surface; **deferred** means deliberately
outside the current slice. Historical low-level APIs are evidence for migration,
not the final v3 contract.

## Raw namespace migration

| Family | Current low-level location | V3 raw target | Migration state | Important remaining work |
| --- | --- | --- | --- | --- |
| Loop and common values | `uv::loop`, common `uv` values | shared `uv::loop`; values remain in `uv` | Implemented target exception | Do not create a second `uv::raw::loop`; classify historical helpers individually. |
| Process | `uv::raw::process` plus `uv::process` owner | `uv::raw::process` / `uv::process` | Implemented namespace split | Complete raw error-policy review and avoid confusing the two native layouts. |
| Filesystem | `uv::fs::raw` requests, results, directory and operations | `uv::raw::fs` | Not migrated | Move the complete protocol together; preserve explicit request cleanup and directory ownership. |
| TCP, pipe, UDP | historical wrappers in `uv`; high-level owners also in `uv` | low-level wrappers in `uv::raw`; owners in `uv` | Not migrated | Resolve name collisions and migrate callback/request helpers with their error contracts. |
| Signal, timer, idle, prepare, check, async, poll, TTY | historical handle wrappers in `uv` | Historical handle wrappers target `uv::raw`; distinct high-level owners, where justified, remain in `uv` | Not migrated | Do not treat the historical wrapper API as the future owner contract. |
| DNS | callback request wrapper in `uv`; high-level `uv::resolve` | request machinery in `uv::raw`; owned result operation in `uv` | Not migrated | Keep native hints/request ownership out of the high-level surface. |
| Generic requests | `uv::request`, connect, write, shutdown, UDP send in `uv` | `uv::raw` | Not migrated | Preserve caller-controlled storage and make submission failures explicit. |
| Work and random | historical request operations in `uv` | raw request protocol in `uv::raw`; future ergonomic operations in `uv`/`uv::ops` | Not migrated | Decide high-level operation shape before treating current names as final. |
| Filesystem watchers | `uv::fs_event`, `uv::fs_poll` in `uv` | raw wrappers in `uv::raw`; future high-level subscription owners in `uv` | Not migrated | Define event retention, stop, close, and consumer-slot semantics first. |
| Threading primitives | `uv` wrappers | Remains to be classified | Deferred | These are synchronous value/ownership wrappers, not automatically raw async resources. |

Namespace migration must not silently change lifetime, allocation, callback, or
error policy. Apply it with the relevant family's contract work rather than as a
blind rename.

## High-level family consistency

| Family | Throwing / `uv::ops` | Ownership and destructor | `resource_scope` | Cancellation | Public native access |
| --- | --- | --- | --- | --- | --- |
| TCP/pipe connection | Throwing I/O; only close currently has an `uv::ops` pair | Stable movable owner; destruction may start close only without incompatible active I/O | Implemented | Pre-submission stop; reads can quiesce; submitted connect/write completes normally | `native`, `native_handle`, `native_stream` |
| TCP/pipe listener | Throwing `accept`; only close currently has an `uv::ops` pair | Stable movable owner; close quiesces accept; active-accept destruction is a contract violation | Implemented | Stop releases one accept; source remains usable | `native`, `native_handle`, `native_stream` |
| UDP socket | Throwing send/receive; only close currently has an `uv::ops` pair | Stable movable owner; active borrowed I/O blocks close | Implemented | Receive can quiesce; submitted send retains state | `native`, `native_handle` |
| DNS | `uv::resolve` / `uv::ops::resolve` | One-shot request state; no persistent owner | Not applicable: no resource owner | Pre-submission cancellation; submitted stop attempts `uv_cancel`; callback remains required | No public persistent native handle |
| Filesystem file | `uv::fs::*` / `uv::ops::fs::*` for open/read/write/close | Stable shared file state; unclosed destruction terminates; native close completion is terminal even on error | Implemented | Submitted request attempts `uv_cancel`; buffer/request live to callback | Descriptor access only as documented; no `uv_handle_t` |
| Signal source | Throwing `next` / `uv::ops::next`; paired close | Stable movable subscription owner; destruction stops and closes asynchronously | Not implemented; absence explicit pending generic subscription policy | Cancels waiter, not subscription; close is terminal | `native`, `native_handle` |
| Process | Throwing `wait`; `uv::ops::wait`, `kill`, and close | Stable movable owner; destruction before exit retains state, then closes | Not implemented; live-child cleanup and supervision policy are unresolved | Cancels waiter, never the child; exit remains retained | `native`, `native_handle` |
| Root task | Throwing result observation; no `uv::ops` resource pair | `spawn_handle` owns root state; active destruction currently terminates, target retains after requesting stop | Not applicable; `task_scope` owns child executions | Cooperative stop; native work still requires terminal completion | No native handle |

The open construction-policy question is:

> Should fallible high-level resource construction also have an explicit-result
> factory under `uv::ops`?

Until this is decided, throwing constructors for listeners, sockets, signals, and
processes do not imply that an explicit-result creation surface has been rejected.

## Slots, inputs, and documentation state

| Family | Concurrency slots and `UV_EBUSY` | Buffer/input ownership | Documentation state |
| --- | --- | --- | --- |
| TCP/pipe connection | One inbound and one outbound slot; same-direction overlap is `UV_EBUSY`; duplex is allowed | Reads and writes borrow buffers; explicit copy/ownership names are required | Implemented experimental owner; raw migration and broader operations target/deferred |
| TCP/pipe listener | One accept slot; competing accept is `UV_EBUSY` | Bind/listen inputs prepared by construction; accepted connection is owned | Implemented experimental owner; queueing/backpressure deferred |
| UDP socket | One receive and one send slot; same-direction overlap is `UV_EBUSY` | Receive and send buffers are borrowed; peer address is copied into the result | Implemented experimental owner; raw migration incomplete |
| DNS | One operation per awaiter; no persistent-owner slot | Node, service, and options copied; returned addresses owned | Implemented high-level operation; raw migration incomplete |
| Filesystem file | One read and one write slot; same-direction overlap and close during active I/O report `UV_EBUSY` | Default read/write spans borrowed; `read_owned` owns destination; `write_copy` owns source | Implemented initial file slice; broader filesystem deferred |
| Signal source | One waiter slot; competing `next` reports `UV_EBUSY`; one pending signal coalesced | No caller buffer; signal number copied | Implemented experimental owner; scope adoption deferred |
| Process | One waiter slot; competing `wait` reports `UV_EBUSY`; exit result retained | Options copied/prepared for synchronous spawn; no borrowed async payload in current slice | Implemented experimental owner; stdio, supervision, and scope adoption deferred |
| Root task | Multiple join observers; result consumption is single-use | Coroutine frames do not extend external borrows | Partially implemented; post-handle retention and failure routing are target work |

Update this registry whenever a family changes namespace, adds an error-policy
pair, changes its terminal protocol, or expands its scope/cancellation surface.
