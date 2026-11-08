# Cancelled raw `closedir` loses its typed directory owner

- **Status:** confirmed source/documentation gap; cancellation reproduction not run
- **Severity:** P2
- **Component:** `uv::raw::fs::closedir`
- **Affected versions:** current repository revision; libuv versions supporting `uv_fs_closedir`
- **Upstream:** libuv documents cancellation behavior; no upstream defect identified.

## Observation

`detail::submit_closedir()` releases the `directory` owner as soon as
`uv_fs_closedir()` accepts submission. Libuv documents that when the request is
cancelled, the directory remains open and the caller must clean the cancelled
request and call `uv_fs_closedir()` again. The runtime callback receives only a
`status_result`; the static callback also receives no directory owner. The
wrapper therefore consumes the typed owner on a path where the native resource
survives and must be closed again.

The raw request exposes `ptr()` and `native_request()`, so a caller may be able
to recover the native `uv_dir_t*` before request cleanup or save it before the
move. That is an untyped escape hatch, not a documented restoration of the
`directory` owner. The user guide currently says that accepted submission
transfers the directory through completion without describing the cancelled
completion case.

## Evidence

- [`submit_closedir`](../../include/uvpp/fs/operations.hpp) calls `dir.release()`
  after any accepted submission, without observing terminal completion.
- [`fs::request`](../../include/uvpp/requests/fs.hpp) has no typed directory
  cancellation result or owner restoration operation.
- [libuv's `uv_fs_closedir` documentation](https://docs.libuv.org/en/v1.x/fs.html#c.uv_fs_closedir)
  says a cancelled request leaves the directory open and must be retried after
  request cleanup.
- [The raw filesystem guide](../user/filesystem.md#raw-filesystem-protocol)
  describes submission-time ownership transfer but not cancellation recovery.

## Reproduction

Open a directory, consume `opendir_result` into `directory`, submit
`closedir(std::move(dir), ...)`, and call `uv_cancel(request.native_request())`
while the request is pending. If cancellation is accepted, the completion status
is `UV_ECANCELED` and libuv leaves the directory open. The typed owner passed to
`closedir` is already empty; a caller must have retained or recovered the native
pointer to submit another close. This was not executed during the audit.

## Recommended resolution

Define a typed terminal ownership contract for `closedir`. Prefer returning or
restoring the directory owner on cancellation so the caller can retry without
relying on `void*` request internals. At minimum, document that accepted
submission is not terminal ownership transfer under cancellation and provide a
typed way to recover the still-open directory. Keep native cancellation
available and preserve libuv's retry requirement.
