# libuv filesystem polling can miss changes before its first stat

- **Status:** confirmed by source and upstream discussion; runtime reproduction not run; upstream API removed
- **Severity:** P2
- **Component:** libuv `uv_fs_poll_start` / `uv_fs_poll_cb`
- **Affected versions:** libuv versions that expose the filesystem-poll API through 1.52.x; removed in 1.53.0
- **Upstream:** [issue #4543](https://github.com/libuv/libuv/issues/4543) was closed as completed after [PR #5063](https://github.com/libuv/libuv/pull/5063) removed `uv_fs_poll_t`; the removal is present in [libuv v1.53.0](https://github.com/libuv/libuv/blob/v1.53.0/include/uv.h).

## Observation

`uv_fs_poll_start()` submits its first `uv_fs_stat()` asynchronously. In the
completion path, libuv calls the user callback only when there is already a
previous baseline (`busy_polling != 0`) and the next stat reports a change. The
first successful stat only establishes that baseline. A file change after
`start()` but before that first stat completes can therefore be absorbed into
the baseline without a user notification.

The upstream issue discusses the mismatch between the callback documentation
and when monitoring becomes observable. One maintainer initially disputed the
full lost-change account, then confirmed that the first stat is asynchronous;
the issue was closed after the API was removed rather than by correcting the
existing API. The source establishes the baseline suppression, but this audit
did not independently reproduce the user-visible race.

## Evidence

- [`uv_fs_poll_start` in libuv 1.52.1](https://github.com/libuv/libuv/blob/v1.52.1/src/fs-poll.c)
  submits the first `uv_fs_stat()` before marking the handle active.
- The poll completion path in the same source suppresses callback delivery when
  `busy_polling` is zero, then stores the first result as the baseline.
- [Upstream issue #4543](https://github.com/libuv/libuv/issues/4543) records the
  semantic concern, maintainer discussion, and API removal.
- `uv::raw::fs_poll` passes starts to libuv and must not emulate a presumed fix.

## Native reproduction

Build against libuv 1.52.x. Start filesystem polling on a file and change it
immediately, before the first asynchronous stat completion. To make the window
more repeatable, occupy the libuv thread pool before starting the poll and
perform the file change while its initial stat is queued. Observe whether the
first stat establishes the changed content as baseline without a callback.
Run this outside uvpp to separate native behavior from wrapper behavior. This
reproducer was not executed during the audit.

## Recommended resolution

Do not add a uvpp workaround. Treat the native behavior as upstream-specific,
avoid depending on a notification for changes before the initial stat completes,
and use the libuv version's available API surface. Since upstream removed
`uv_fs_poll_t` in 1.53.0, uvpp should version-gate the wrapper rather than
preserving the old behavior under a new implementation.
