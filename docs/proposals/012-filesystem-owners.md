# Filesystem Owners and Coroutine I/O

Status: partially implemented.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Dependencies: [002 — Asynchronous ownership](002-async-ownership.md),
[004 — Shared operation state](004-operation-state.md), [005 — Buffers and flow
control](005-buffers-and-flow-control.md), and [006 — Errors and
results](006-errors-and-results.md).

## Goal

Provide a high-level filesystem owner and paired throwing/result coroutine
operations without imposing caller-owned `uv_fs_t` storage. Existing callback
filesystem APIs remain available during the raw namespace migration; their
default `read` and `write` borrow caller storage, while `read_owned` and
`write_copy` make ownership explicit.

## Implemented vertical slice

`<uvpp/fs/coroutines.hpp>` now provides a movable `uv::fs::file`, opened by
`uv::fs::open(path, flags, mode)`, and the explicit-result equivalent
`uv::ops::fs::open(...)`. `read`, `write`, and `close` have matching pairs.
They run on the awaiting task's inherited `uv::loop`; an owner used from another
loop is a programmer error.

The owner allocates stable state before `uv_fs_open` submission. Each submitted
operation keeps its `uv_fs_t` in its coroutine frame and uses private native
address recovery, leaving `uv_req_t::data` for application code. Native callback
delivery, and every immediate native-submission failure, call
`uv_fs_req_cleanup` before task delivery. A pre-existing stop reports
`UV_ECANCELED` without submission. After submission, cancellation only requests
`uv_cancel`; the request and borrowed buffer remain valid until libuv's terminal
callback.

Read returns `file_read_result { count(), eof() }`; EOF is distinct from an
empty supplied buffer. Write returns a byte count and does not hide short
completion. One read and one write may overlap, while a competing operation in
the same direction reports `UV_EBUSY`. Both buffers are borrowed through actual
completion. `close` rejects either active direction and joins concurrent close
awaiters.

`uv_fs_close` has a distinct terminal contract. Submission failure leaves the
owner open and retryable. Once libuv has invoked the close callback, the owner
forgets the descriptor regardless of its completion status. Retrying then could
close a descriptor number reused by the operating system. Repeated close awaits
return the recorded terminal status. The destructor diagnoses an unclosed owner
or active borrowed I/O. `resource_scope` owns files, returns a `file_view`, and
releases an owner after terminal close before delivering a close error.

Tests in [`test-co.cpp`](../../tests/test-co.cpp) cover a normal round trip,
explicit open/write failures, and scope cleanup.

## Remaining work

- Decide the supported fallback when an unclosed standalone owner is destroyed;
  the current diagnostic is deliberately strict.
- Add deterministic fault injection for submission and close-completion errors,
  cancellation races, concurrent close waiters, and same-direction exclusion.
- Design owning reads, scatter/gather I/O, copying helpers, queueing, directory
  ownership, metadata operations, and close-error aggregation.
- Complete the `uv::raw::fs` namespace migration and adapt the callback API to
  the common operation protocol where useful.
