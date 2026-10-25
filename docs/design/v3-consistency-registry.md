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
| Filesystem | `uv::raw::fs` requests, results, directory and operations | `uv::raw::fs` | Implemented namespace and submission-error migration | Submission returns `uv::status`; completion retains typed results. Explicit request cleanup and directory close remain caller-controlled. |
| TCP handle | `uv::raw::tcp`; independent high-level `tcp_connection`/`tcp_listener` | Raw handle in `uv::raw`; owners in `uv` | TCP operations, stream subscriptions and requests migrated | Init/open/bind/options/reset return status, address/descriptor/buffer queries retain results. Normal close releases handle callbacks; requests retain their own completion storage. Connect/write/shutdown submission returns status with separately retained raw requests. |
| Stream substrate | `uv::raw::stream<Derived, Native>` | `uv::raw::stream` | Persistent operations migrated | Listen/read start/stop/accept/blocking return status. Read allocator/reader stay paired through native loans; runtime/static replacement and all close forms release active slots. Write/shutdown submission returns status with separately retained requests. Pipe-specific methods are migrated; TTY family-specific methods remain transitional. |
| Pipe | `uv::raw::pipe`; independent high-level owners in `uv` | Raw handle in `uv::raw`; owners in `uv` | Implemented namespace and operational-error migration | Explicit init; status setup/connect/IPC submissions; `result<string>` names; native pending observations and typed immediate writes retained. Legacy name copies can throw separately. |
| UDP | historical wrapper and high-level owners in `uv` | low-level wrapper in `uv::raw`; owners in `uv` | Not migrated | Next network slice; keep handle/request and typed-outcome boundaries. |
| Signal, poll, fs-event | `uv::raw::{signal,poll,fs_event}` plus distinct high-level sources | Raw wrappers in `uv::raw`; sources remain in `uv` | Implemented namespace and operational-error migration | Start/stop return `status`; native queries return results. All three support default construction and explicit `init()` with native-loop-pointer tracking; poll rolls back incomplete native registration on init failure. Terminal slot release, rollback, and reentrant replacement are implemented; broader platform/cost validation remains. |
| Timer, idle, prepare, check | `uv::raw::{timer,idle,prepare,check}` | Raw wrappers in `uv::raw`; coroutine sleep remains in `uv::co` | Implemented namespace and operational-error migration | Start/stop and timer reconfiguration return `status`; shared native queries return results. All four support default construction and explicit `init()` with native-loop-pointer initialization tracking and shared pre-init guards. One-shot timer delivery releases its callback; phase restarts replace native dispatch. Broader platform/cost validation remains. |
| Async | `uv::raw::async`; high-level `uv::loop_posting` is independent | Raw notification handle in `uv::raw` | Implemented namespace and operational-error migration | Default construction and explicit empty/runtime/static `init()` share native-loop-pointer tracking and pre-init guards; constructors unwrap status. Cross-thread `send()` checks only the immutable initialization sentinel, without reading `closing()`; caller must quiesce producers before close. Runtime/static setters and native queries use results; callbacks remain loop-thread only. Broader platform/cost validation remains. |
| TTY | historical handle wrapper in `uv` | `uv::raw::tty`; future owners, if justified, remain separate | Not migrated | Review descriptor and terminal configuration/error contracts with this family. |
| DNS | callback request wrapper in `uv`; high-level `uv::resolve` | request machinery in `uv::raw`; owned result operation in `uv` | Not migrated | Keep native hints/request ownership out of the high-level surface. |
| Requests | `uv::raw::{connect_request,write_request,shutdown_request}`; generic/UDP requests still in `uv` | `uv::raw` | Stream/TCP request migration implemented; others deferred | Pending guards, explicit submission statuses and pre-delivery slot release implemented. Pipe-specific connect/IPC entry points now share explicit statuses and these raw requests. |
| Work and random | historical request operations in `uv` | raw request protocol in `uv::raw`; future ergonomic operations in `uv`/`uv::ops` | Not migrated | Decide high-level operation shape before treating current names as final. |
| Filesystem polling | Historical `uv::fs_poll` | `uv::raw::fs_poll`; future high-level ownership remains separate | Not migrated | Review callback, interval, borrowed-stat, and error contracts with this family. |
| Threading primitives | `uv` wrappers | Remains to be classified | Deferred | These are synchronous value/ownership wrappers, not automatically raw async resources. |

Namespace migration must not silently change lifetime, allocation, callback, or
error policy. Apply it with the relevant family's contract work rather than as a
blind rename.

## High-level family consistency

| Family | Throwing / `uv::ops` | Ownership and destructor | `resource_scope` | Cancellation | Public native access |
| --- | --- | --- | --- | --- | --- |
| TCP/pipe connection | Paired read/write/close; connect and IPC remain throwing | Stable movable owner; destruction may start close only without incompatible active I/O | Implemented | Pre-submission stop; reads can quiesce; submitted connect/write completes normally | `native`, `native_handle`, `native_stream` |
| TCP/pipe listener | Paired construction (`make_*`), accept, and close | Stable movable owner; close quiesces accept; active-accept destruction is a contract violation | Implemented | Stop releases one accept; source remains usable | `native`, `native_handle`, `native_stream` |
| UDP socket | Paired construction (`make_udp_socket`), send/receive, and close | Stable movable owner; active borrowed I/O blocks close | Implemented | Receive can quiesce; submitted send retains state | `native`, `native_handle` |
| DNS | `uv::resolve` / `uv::ops::resolve` | One-shot request state; no persistent owner | Not applicable: no resource owner | Pre-submission cancellation; submitted stop attempts `uv_cancel`; callback remains required | No public persistent native handle |
| Filesystem file | `uv::fs::*` / `uv::ops::fs::*` for open/read/write/close | Stable shared file state; unclosed destruction terminates; native close completion is terminal even on error | Implemented | Submitted request attempts `uv_cancel`; buffer/request live to callback | Descriptor access only as documented; no `uv_handle_t` |
| Poll source | Throwing `next` / `uv::ops::next`; paired close | Stable movable poll owner; descriptor borrowed; destruction starts terminal close | Not implemented | Cancels active wait; source stays reusable; close is terminal | `native`, `native_handle` |
| Filesystem event source | Throwing `next` / `uv::ops::next`; paired close | Stable movable subscription owner; destruction stops and closes asynchronously; filesystem object external | Not implemented | Cancels waiter, not watcher; close is terminal | `native`, `native_handle` |
| Signal source | Throwing `next` / `uv::ops::next`; paired close | Stable movable subscription owner; destruction stops and closes asynchronously | Not implemented; absence explicit pending generic subscription policy | Cancels waiter, not subscription; close is terminal | `native`, `native_handle` |
| Process | Throwing `wait`; `uv::ops::wait`, `kill`, and close | Stable movable owner; destruction before exit retains state, then closes | Not implemented; live-child cleanup and supervision policy are unresolved | Cancels waiter, never the child; exit remains retained | `native`, `native_handle` |
| Root task | Throwing result observation; no `uv::ops` resource pair | `spawn_handle` observes root state; active destruction requests stop while a completion baton retains through terminal completion | Not applicable; `task_scope` owns child executions | Cooperative stop; native work still requires terminal completion | No native handle |
| Loop posting | Throwing endpoint `post` / `uv::ops::post`; same-loop close await | Non-movable owner; endpoints share stable state; destruction before native close completion terminates | Not implemented | Close seals admission and drains; no cancellation of accepted work or close | No native accessor; internal `data` reserved |

Synchronous fallible owner construction pairs throwing constructors with
`uv::ops::make_*` result factories. TCP/pipe listeners and UDP sockets implement
this policy, sharing native setup and asynchronous rollback. C++ allocation and
preparation failures still throw. Other synchronous owner families retain throwing
construction pending their own implementation slices; see
[006](../proposals/006-errors-and-results.md).

## Slots, inputs, and documentation state

| Family | Concurrency slots and `UV_EBUSY` | Buffer/input ownership | Documentation state |
| --- | --- | --- | --- |
| TCP/pipe connection | One inbound and one outbound slot; same-direction overlap is `UV_EBUSY`; duplex is allowed | Reads and writes borrow buffers; explicit copy/ownership names are required | Implemented experimental owner; raw migration and broader operations target/deferred |
| TCP/pipe listener | One accept slot; competing accept is `UV_EBUSY` | Bind/listen inputs prepared by construction; accepted connection is owned | Implemented experimental owner; queueing/backpressure deferred |
| UDP socket | One receive and one send slot; same-direction overlap is `UV_EBUSY` | Receive and send buffers are borrowed; peer address is copied into the result | Implemented experimental owner; raw migration incomplete |
| DNS | One operation per awaiter; no persistent-owner slot | Node, service, and options copied; returned addresses owned | Implemented high-level operation; raw migration incomplete |
| Filesystem file | One read and one write slot; same-direction overlap and close during active I/O report `UV_EBUSY` | Default read/write spans borrowed; `read_owned` owns destination; `write_copy` owns source | Implemented initial file slice; broader filesystem deferred |
| Poll source | One waiter; competing `next(mask)` reports `UV_EBUSY`; native polling stops before each delivery | Descriptor borrowed; no pending readiness queue; per-wait mask | Implemented experimental owner; scope adoption and persistent subscription deferred |
| Filesystem event source | One waiter; competing `next` reports `UV_EBUSY`; one pending outcome coalesced | Temporary constructor path copy; owned optional filename | Implemented experimental owner; scope adoption and broader platform validation deferred |
| Signal source | One waiter slot; competing `next` reports `UV_EBUSY`; one pending signal coalesced | No caller buffer; signal number copied | Implemented experimental owner; scope adoption deferred |
| Process | One waiter slot; competing `wait` reports `UV_EBUSY`; exit result retained | Options copied/prepared for synchronous spawn; no borrowed async payload in current slice | Implemented experimental owner; stdio, supervision, and scope adoption deferred |
| Root task | Multiple join observers; result consumption is single-use | Coroutine frames do not extend external borrows | Implemented experimental contract: active-handle destruction retains through terminal completion and routes unobserved failures exactly once; public detach and cross-thread control remain deferred |
| Loop posting | Bounded queue, mutex admission, bounded drain batch; multiple close waiters | Callables owned, move-only supported; captured references remain borrowed | Implemented experimental posting; producer/close races and endpoint lifetime tested; continuation scheduling remains deferred |

Update this registry whenever a family changes namespace, adds an error-policy
pair, changes its terminal protocol, or expands its scope/cancellation surface.
