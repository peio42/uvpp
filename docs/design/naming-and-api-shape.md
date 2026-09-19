# Naming and API Shape

Status: code review and proposed convention for v3 consolidation. Architectural
rules already accepted in [000](../proposals/000-v3-architecture.md) remain binding;
the recommendations and decisions below are not an API freeze. This document
does not rename existing symbols or settle unfinished lifetime contracts.

## Scope and evidence

The current branch combines historical low-level APIs with experimental v3
owners and coroutine operations. Review both, but do not infer the v3 convention
from a historical spelling alone. In particular, the old `try_*` error-policy
rule and `uv::fs::raw` placement are migration context.

The review covers public declarations in `core/`, `handles/`, `requests/`,
`net/`, `fs/`, and `co/`, with particular attention to the implemented TCP,
pipe, UDP, and scope prototypes. Relevant implementation references:

| Area | Source | Review conclusion |
| --- | --- | --- |
| Stream owners | [TCP](../../include/uvpp/net/tcp_connection.hpp), [pipe](../../include/uvpp/net/pipe_connection.hpp) | Keep `tcp_connection`, `pipe_connection`, `connect`, `read_some`, and `write`. |
| Producers | [TCP listener](../../include/uvpp/net/tcp_listener.hpp), [pipe listener](../../include/uvpp/net/pipe_listener.hpp) | Keep `*_listener` and `accept`; constructors currently bind/listen. |
| Datagram owner | [UDP](../../include/uvpp/net/udp_socket.hpp) | Keep `udp_socket`, `send_to`, and `recv_from`; review address accessors. |
| Scoped access | [resource scope](../../include/uvpp/co/resource_scope.hpp) | Distinguish the owning scope, non-owning registrations, and borrowed views. |
| Execution | [tasks](../../include/uvpp/co/task.hpp), [task scope](../../include/uvpp/co/task_scope.hpp) | Keep task vocabulary; current `spawn_handle<T>` owns a spawned `task<T>`; `task_scope` children remain `task<void>`. |
| Native wrappers | [handles](../../include/uvpp/handles/handle.hpp), [streams](../../include/uvpp/handles/stream.hpp), [requests](../../include/uvpp/requests/request.hpp) | Existing low-level types still occupy `uv`; namespace migration is unfinished. |
| Filesystem | [operations](../../include/uvpp/fs/operations.hpp), [descriptor](../../include/uvpp/fs/file.hpp) | Current `file_descriptor` is a copyable descriptor value, not an asynchronous file owner. |
| Results | [error vocabulary](../../include/uvpp/core/error.hpp), [stream I/O](../../include/uvpp/detail/stream_io.hpp) | Result names do not imply one uniform value/error interface. |

Existing usage and lifecycle coverage can be found in
[coroutine tests](../../tests/test-co.cpp), [TCP tests](../../tests/test-tcp.cpp),
[pipe tests](../../tests/test-pipe.cpp), and [UDP tests](../../tests/test-udp.cpp).
This is a naming and surface review, not a fresh lifecycle validation.

## Established architectural rules

Use `snake_case` for public types, functions, and enumerators. Keep `uvpp` for
the project and include root, `uv` for the C++ API, and `UVPP_*` for project
macros. Capability macros describe native capabilities, as specified in
[API policy decisions](api-policy-decisions.md#version-gated-libuv-features).

| Namespace | V3 responsibility |
| --- | --- |
| `uv` | Shared vocabulary, common `loop`, high-level owners and ergonomic operations |
| `uv::raw` | Low-level handles, requests, and explicit native protocols |
| `uv::fs` / `uv::raw::fs` | High-level / raw filesystem operations |
| `uv::ops` (including `uv::ops::fs`) | Explicit-result operations on the same high-level owners |
| `uv::co` | Tasks, spawning, scopes, cancellation, and coroutine composition |
| `detail` within the relevant namespace | Implementation machinery |

Do not add another loop owner or a coroutine-specific I/O hierarchy. Header
directories do not dictate namespaces: `net/tcp_connection.hpp` need not imply
`uv::net::tcp_connection`. Additional `net`, `io`, or `ipc` public namespaces
need a demonstrated organizational benefit.

Use plain operation names across error surfaces. Do not select error policy
with `_status`, `_ec`, `async_`, or `try_`. Reserve `try_*` for actual attempts,
such as `try_lock`. The `uv::ops` surface is a target, not an available complete
API; see [006](../proposals/006-errors-and-results.md). Explicit results do not
automatically imply `noexcept`.

Borrowing is the default for write/send payloads. Copying or ownership transfer
requires an explicit semantic name or type. `write_copy` is illustrative and
remains proposed. `write_with_handle` names an additional IPC effect; it does
not mean transferring the source C++ owner.

Borrowed views use explicit `.view()` production, never implicit conversion.
Native access is named and explicit, never an implicit pointer conversion.
It is borrowed access: it neither transfers ownership nor permits mutation of
libuv fields or callback/data slots managed by uvpp, unless a specific API
documents that mutation. Public owners expose `native()` for their concrete
native representation, `native_handle()` for `uv_handle_t*`, and
`native_stream()` for `uv_stream_t*` where applicable.
Public duration values use chrono; counters remain counts.

## Recommended type vocabulary

Name a high-level object after its resource or role. Do not append `_handle`,
`_owner`, or `_impl` merely to describe how it is implemented.

| Name | Meaning |
| --- | --- |
| `*_connection` | A point-to-point byte-stream resource obtained by connect/accept; the name does not promise perpetual connectedness after EOF or close. |
| `*_listener` | A resource that accepts new connections. |
| `*_socket` | A socket resource whose useful operations do not require a stream session, currently UDP. |
| `*_view` | Borrowed access that does not retain ownership of the resource. |
| `*_request` | Explicit request storage/protocol, especially in the raw layer. |
| `*_registration` | Access associated with a scope registration; not an independent resource owner. |
| `*_scope` | Ownership and a structured completion/cleanup boundary, whose asynchronous exit must be documented. |
| `*_options` | A configuration value; use a separate builder only for a distinct invariant or lifetime. |

Keep `stream` as capability vocabulary without requiring a new concrete owning
`stream` type or public inheritance hierarchy. TCP and pipe currently share
internal I/O machinery; this alone does not establish a public stream concept.

`*_view` never owns a native lifetime or initiates cleanup through destruction.
It may become invalid when the underlying owner or scope finishes. Diagnostic
tokens may retain their own bookkeeping only; they must not retain the resource
state or extend its native lifetime.

`handle` remains appropriate when it describes a meaningful capability, such as
`spawn_handle`, or native identity, such as `handle_view`. Do not create views
for every owner mechanically. Current TCP/pipe/UDP high-level views come from
resource-scope registrations; the owners do not all expose `.view()`. Listener
registrations expose `accept()` directly and are not listener-view types.

A shared diagnostic token does not turn a borrowed view into a resource owner.
Likewise, `file_descriptor`, `socket_address`, and `buffer_view` must not imply
automatic resource close. Keep `owned_buffer` because ownership distinguishes it
from the buffer view.

## Recommended operation shape

Use verbs for effects and nouns for observations. Retain clear domain vocabulary:
`connect`, `accept`, `read_some`, `write`, `send_to`, `recv_from`. Do not add
`async_` or `co_` just because an operation returns an awaitable.

- `read_some` denotes a bounded read, not an exact-fill promise. Preserve EOF
  and partial-progress information in the contract.
- `*_now` identifies immediate I/O attempts such as `write_now` and `send_now`.
  It is not a suffix for every synchronous getter, configuration call, or factory.
- `*_start` / `*_stop` remain useful for persistent native subscriptions;
  do not rename them to one-shot operations solely for visual consistency.
- `take_*` denotes extraction/ownership transfer, as in `take_tcp()`.
  `view()` and native access do not transfer ownership.
- `request_stop()` requests cancellation; `join()` waits for tasks;
  `resource_scope::finish()` performs asynchronous resource cleanup. These
  distinct effects should retain distinct names.
- Prefer observations such as `size()`, `closing()`, and `done()` without
  mechanical `get_` or `is_` prefixes. Use `has_*` for possession and keep
  standard vocabulary such as `empty()`. A predicate's exact state semantics
  still need documentation.

Use a member when operating on an existing resource, a static factory when
producing that resource naturally belongs to its type, and a free function for
an operation with no persistent receiver. Current `tcp_connection::connect`
and `pipe_connection::connect` are factories. A future DNS operation need not
introduce a persistent `resolver` without persistent state that justifies it.
This does not choose its final spelling or require converting all constructors
to factories.

Awaiter spellings are machinery, not the primary user vocabulary. Prefer
examples with `auto` and `co_await`; do not promise that currently public nested
`*_awaiter` types are a stable extension interface.

## Result names and placement

Use `<operation>_result` for operation-specific returned information when no
better domain name exists. Retain domain names for independently meaningful
values or resources, such as `socket_address` or a returned connection. Do not
require every return type to end in `_result`.

`_result` does not promise ownership, an error carrier, or `noexcept` delivery.
Current `tcp_connection::read_some_result` and its pipe counterpart are aliases
of one internal value carrying a byte count and EOF. UDP's nested
`recv_from_result` carries a size, copied peer address, and partial flag;
operational failures are thrown by the await. Historical `uv::result<void>` is a
non-template status type. A unified result carrier remains a decision in 006.

Prefer namespace-level names for vocabulary genuinely shared by several APIs;
keep a type nested when its meaning is local to one owner or operation family.
Do not expose a `detail` name as the recommended user spelling. Whether shared
stream results should gain one public namespace-level type is still open.

Keep typed enum conventions: `*_mode` for exclusive modes, `*_flag` for masks,
`*_type` for primary classifications, and `*_event` / `*_event_kind` for events.
Do not rename domain classifications mechanically merely to remove `kind`.

## Decisions recorded before public API freeze

| Finding in current code | Decision / remaining scope |
| --- | --- |
| Former `received_handle` stored a queue of TCP owners and the bytes accumulated by `receive_handle()`. | Renamed to `receive_handle_result`: it is the result of an operation, has operation metadata, and permits repeated `take_tcp()` extraction. Its `receive_handle_kind` classification remains specific to the result. |
| New owners exposed a concrete pointer as `native_handle()`. | Owners now use `native()` for `uv_tcp_t*`, `uv_pipe_t*`, or `uv_udp_t*`; `native_handle()` returns `uv_handle_t*`; stream owners additionally expose `native_stream()`. This access borrows and preserves uvpp-managed fields and callback slots. |
| TCP listener and UDP socket exposed `sockname()`. UDP receive exposed separate IPv4/IPv6 peer accessors. | High-level owners use `local_address()`. `udp_socket::recv_from_result::peer_address()` returns the shared value type `socket_address`. The raw layer may retain libuv terminology where appropriate. Connections may add `local_address()` and `remote_address()` when those observations are implemented. |
| TCP/pipe connections originally had only internal close completion, while listeners and UDP had synchronous `void close()`. | Implemented experimentally for the current high-level owners: `close()` is awaitable and completes only after native close; `request_close()` explicitly initiates close without awaiting. The contract, including active-operation rules, remains in [002](../proposals/002-async-ownership.md). |
| Scope registrations differ: connection/socket registrations produce views, listener registrations expose `accept()`. | Settled: retain the capability difference and add no listener view without a concrete borrowing use case. |
| `spawn_handle<T>` owns a root frame, exposes multi-observer same-loop `join()`, cooperative `request_stop()`, and a single-consumer non-void `take_result()`. | Keep the name provisionally. Active destruction still terminates; implicit detach, post-destruction retention, and unobserved-error routing remain open in [003](../proposals/003-cancellation-and-task-scopes.md). |
| Historical `try_close`, request `try_cancel`, low-level `uv::tcp`, and `uv::fs::raw` remain. | Settled: migrate with the relevant error, close, or namespace change, not in an isolated rename-only branch. |

Future timer, signal, process, TTY, poll, DNS, and filesystem owners must be named
from their actual contracts. Existing historical names do not establish those
future high-level contracts. In particular, choose a high-level async-notification
name (`wakeup`, `async_signal`, or another name) only after deciding whether it
represents a notification, a queue, or scheduling. Do not prematurely reserve
`uv::file` in conflict with the accepted `uv::fs` domain organization.

The next consolidation should confirm these decisions, then update code, tests,
user documentation, and proposal progress together. Freezing naming rules does
not waive the lifecycle and performance gates in
[010](../proposals/010-validation-and-performance.md).
