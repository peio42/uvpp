# Raw filesystem requests do not reject reuse while pending

- **Status:** confirmed by source inspection; misuse reproduction not run
- **Severity:** P2
- **Component:** `uv::raw::fs::request` and every operation in `fs/operations.hpp`
- **Affected versions:** current repository revision; libuv versions with `uv_fs_t`
- **Upstream:** no native defect identified. Reusing a pending native request is outside libuv's request lifetime contract.

## Observation

Raw DNS, random, worker, and stream request paths track accepted submissions and
reject reuse with `UV_EBUSY`. `uv::raw::fs::request` has no pending state or
submission guard. Runtime operations replace the stored callback before
submission; static-callback operations have no wrapper state at all. A second
filesystem operation can therefore reinitialize the same `uv_fs_t` before its
first completion, overwrite the runtime callback, and invalidate native request
state still owned by libuv.

The filesystem guide tells callers to clean a request after completion and
before reuse, so valid callers can avoid this path. The raw API still differs
from the neighboring request families that report pending state and reject the
same misuse consistently across runtime and static callback forms.

## Evidence

- [`requests/fs.hpp`](../../include/uvpp/requests/fs.hpp) stores a callback but
  has no `pending()` state or cancellation/submission lifecycle marker.
- [`fs/operations.hpp`](../../include/uvpp/fs/operations.hpp) installs that
  callback before every native submission; static submissions bypass callback
  storage and likewise do not check whether the native request is in flight.
- [`requests/dns.hpp`](../../include/uvpp/requests/dns.hpp),
  [`requests/random.hpp`](../../include/uvpp/requests/random.hpp), and
  [`requests/work.hpp`](../../include/uvpp/requests/work.hpp) check pending state
  for both runtime and static submission.
- [The filesystem guide](../user/filesystem.md#raw-filesystem-protocol) documents
  completion and cleanup before reuse.

## Reproduction

Submit an asynchronous `stat` using one `request`, then submit a second
asynchronous filesystem operation with the same request before running the loop
to completion. The wrapper does not return `UV_EBUSY`; libuv's request storage
and the first callback slot are overwritten. The native behavior is invalid
request reuse, so a sanitizer or debug libuv build is appropriate for a focused
reproducer. This was not executed during the audit.

## Recommended resolution

Give raw filesystem requests a pending marker shared by runtime and static
submissions. Set it only when libuv accepts the request, clear it immediately
before terminal user delivery, and roll it back on immediate rejection. Return
`UV_EBUSY` for an in-flight submission while preserving the existing rule that
the caller cleans the completed request before reuse. If intentionally leaving
the precondition unenforced, document that distinction against the other raw
request families.
