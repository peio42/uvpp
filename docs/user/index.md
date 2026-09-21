# uvpp v3 user guide

uvpp is header-only C++20. Include headers from `uvpp/`, use namespace `uv`,
and link libuv and pthread. These guides cover v3 exclusively; the APIs described
as available below are experimental and may change before release.

## Start here

1. [Getting started](getting-started.md): build and run a coroutine task.
2. [Loop](loop.md): execution context, driving, and shutdown.
3. [Coroutines](coroutines.md): cold tasks, spawning, joining, stop, and task scopes.
4. [Ownership and lifetime](ownership-and-lifetime.md): owners, views, close, and resource scopes.
5. [Errors](errors.md): throwing awaits, results, and setup failures.
6. [Buffers](buffers.md): borrowed payloads and completion lifetimes.
7. [Filesystem](filesystem.md): owned files and coroutine I/O.
8. [Signals](signals.md): persistent signal subscriptions and individual waits.

## Networking

- [TCP](networking/tcp.md): connect, accept, stream I/O, and listener limits.
- [UDP](networking/udp.md): datagram send/receive and truncation.
- [Pipe](networking/pipe.md): local streams and TCP handle passing over IPC.
- [DNS](networking/dns.md): coroutine `getaddrinfo` resolution and result surfaces.

## Availability

| Surface | Available on this branch |
| --- | --- |
| Common execution | `uv::loop`, cold `uv::co::task<T>`, `spawn_handle<T>`, join, cooperative stop, timer sleep |
| Structured execution | `uv::co::task_scope` for `task<void>` children |
| Resource cleanup | `uv::co::resource_scope` for TCP/pipe connections and listeners, UDP sockets, and files |
| Network owners | `tcp_connection`, `tcp_listener`, `pipe_connection`, `pipe_listener`, `udp_socket` |
| Signal owner | `signal_source`, persistent `next()`, `uv::ops::next`, and terminal close |
| Explicit-result operations | `uv::ops::close(owner)`, `uv::ops::resolve(...)`, and `uv::ops::fs::{open,read,write,close}` |
| Raw layer | `uv::raw` is the target namespace; the low-level namespace/error-policy migration is unfinished |
| Filesystem | `uv::fs::{open,read,write,close}` with `uv::ops::fs` result counterparts; directories and broader filesystem operations remain proposed |
| Other domains | Process, watchers, threading and utility code exists, but their v3 surfaces have not been consolidated in these guides |

Do not infer that a header's presence establishes the final v3 API. In particular,
there is no documented v3 `write_copy`, `read_exactly`, general detached task,
implicit server task spawning, or bounded I/O queue yet. Remaining design work
and validation gates are tracked in [proposals](../proposals/index.md).
