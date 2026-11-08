# Raw v3 post-migration audit summary

- **Status:** source audit complete; runtime/platform reproduction not performed
- **Date:** 2026-11-08
- **Scope:** `uv::raw` handles, requests, filesystem operations, DNS, process,
  work, random, stream I/O, native views, callback slots, and public headers
- **Design references:** proposals 000, 002, 004, 005, 006, 010, and 012; the
  native-semantics, ownership, callback, error, and thread-safety design docs

## Priorities

1. **P1 — Restore versioned header compatibility.** The unconditional raw
   filesystem-poll include prevents normal umbrella-header use with libuv 1.53,
   where upstream removed the complete API. See
   [the compatibility finding](raw-fs-poll-libuv-1.53.md).
2. **P2 — Tighten raw filesystem request lifecycle.** Filesystem requests lack
   a pending guard shared by DNS, random, work, and stream request families;
   accepted `closedir` cancellation also leaves the directory open after its
   typed owner has been consumed. See the [request reuse](raw-fs-request-pending-reuse.md)
   and [closedir ownership](raw-closedir-cancellation-ownership.md) findings.
3. **P2 — Review active filesystem-poll restart status.** The wrapper returns
   `UV_EBUSY` where native libuv reports success and leaves the active
   subscription unchanged. This is an intentional, documented policy, but it
   diverges from the `uv::raw` native-semantics contract. See
   [the restart finding](raw-fs-poll-active-restart.md).
4. **P3 — Make duplicate close behavior consistent.** Watcher families call
   `uv_close` again on duplicate close while stream, process, and UDP paths
   guard it. See [the close finding](raw-watcher-close-consistency.md).
5. **Track the upstream filesystem-poll issue separately.** Older libuv
   implementations can miss changes before the first asynchronous stat
   establishes a baseline; upstream removed the API in 1.53 rather than fixing
   that contract. See [the upstream finding](upstream-fs-poll-initial-stat.md).

## Areas reviewed

- Native storage layout, immobility, application `data` access, explicit native
  handle/stream/request helpers, and borrowed view/result lifetimes.
- Handle initialization, active/closing guards, stop/restart, callback
  replacement, reentrant delivery, one-shot callback release, close completion,
  and destruction expectations across watchers, streams, sockets, and process.
- Read/write/accept/connect/shutdown/UDP send and receive result paths,
  including partial progress, EOF, would-block, native error propagation, and
  borrowed buffer retention.
- Request submission/completion/cancellation across stream, DNS, filesystem,
  random, and worker requests; callback exceptions at C boundaries; request
  reuse and cleanup expectations.
- Filesystem path conversion, directory ownership and iteration, typed result
  borrowing, and hidden allocation claims.
- Loop-thread affinity, the explicitly cross-thread async send path, process
  exit/close ordering, and worker-thread callback separation.
- Version and platform gates, with special review of filesystem polling against
  libuv 1.53.0.

## Findings and unresolved questions

This audit records five uvpp issues and one upstream issue. The existing
Windows poll rollback finding was also reviewed; it is an intentional, isolated
initialization cleanup backed by upstream source analysis and is not a new audit
finding. Path copies, runtime `std::function` storage, directory read buffers,
and libuv-owned allocations are documented costs; no additional hidden
one-shot-operation allocation was identified in the reviewed raw families.

The repository does not declare a minimum or supported libuv range, while CI
contains a libuv 1.53.0 configuration. Resolve whether 1.53 is supported before
choosing the compatibility boundary. The raw filesystem-poll API removal is an
upstream change, so uvpp should gate or remove that surface rather than emulate
it.

The audit was source-based. It did not run the test suite or platform builds.
Follow-up validation should add focused native reproductions for request reuse,
cancelled `closedir`, duplicate close, and pre-first-stat filesystem changes,
then compile the umbrella header against the project's declared oldest and
newest libuv versions on GCC, Clang, and MSVC/Windows.
