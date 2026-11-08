# Raw watcher close behavior differs by handle family

- **Status:** confirmed source inconsistency; duplicate-close runtime reproduction not run
- **Severity:** P3
- **Component:** raw timer/async/idle/prepare/check/signal/poll/fs-event/fs-poll versus stream/UDP/process handles
- **Affected versions:** current repository revision
- **Upstream:** no native defect identified; repeated `uv_close()` is outside the native close contract.

## Observation

The shared `raw_watcher_handle::close()` overloads call `uv_close()` without
checking `closing()`. `initializable_handle` checks only `initialized()`, which
remains true after close starts. In contrast, raw streams check `ready()`, and
raw process/UDP close paths check for an already-closing handle. A second close
on a watcher therefore reaches libuv's native duplicate-close path, while
families sharing the same raw API silently ignore the repeated call.

This is not a claim that libuv permits repeated close. It is an inconsistent
wrapper policy: users cannot predict whether repeated `close()` is inert or
delegated to libuv based on the common raw handle surface. In assertion-enabled
libuv builds the native `uv_close()` precondition can abort the process.

## Evidence

- [`initializable_handle`](../../include/uvpp/detail/initializable_handle.hpp)
  guards close only with `initialized()`.
- [`raw_watcher_handle`](../../include/uvpp/detail/raw_watcher.hpp) forwards all
  close forms directly to `basic_handle::close()`.
- [`stream::close`](../../include/uvpp/handles/stream.hpp),
  [`process::close`](../../include/uvpp/handles/process.hpp), and
  [`udp::close`](../../include/uvpp/handles/udp.hpp) include already-closing
  guards.
- [libuv's Unix `uv_close()` implementation](https://github.com/libuv/libuv/blob/v1.x/src/unix/core.c)
  asserts that a handle is not already closing.
- The raw watcher guide describes explicit close and native callback lifetime,
  but does not state a duplicate-close precondition.

## Reproduction

Initialize a raw timer, call `close()` twice before driving the loop, and run
against an assertion-enabled libuv build. The second call reaches native
`uv_close()` again. This source-level case was not executed during the audit.

## Recommended resolution

Choose one policy for raw handle close calls. Either make repeated close inert
across handle families, or preserve the native precondition consistently and
document it across all raw handle guides. Keep close callbacks single-delivery
and do not overwrite a pending callback on a duplicate call.
