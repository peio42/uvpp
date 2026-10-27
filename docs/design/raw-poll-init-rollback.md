# Raw poll initialization rollback

`detail::unlink_poll_init_registration` is a **uvpp helper**, not a libuv symbol.
It reproduces removal from libuv's private registration queue. This dependency
exists to preserve the raw contract: failed initialization leaves no registered
handle, requires no asynchronous close, and permits immediate destruction/retry.
Loop-taking throwing constructors use the same path.

## Alternatives and selected boundary

| Approach | Consequence |
| --- | --- |
| Public libuv rollback API | None exists for undoing an incomplete poll init. `uv_poll_stop` only stops polling; it does not remove loop registration. |
| `uv_close` | Requires retaining the native storage until a later close callback. A failed constructor with inline caller-owned storage cannot do that. Driving the loop inside init would violate uvpp's no-nested-loop cleanup contract. |
| Winsock preflight `getsockopt` | May reject some inputs earlier, but does not guarantee that libuv's later query succeeds. It cannot replace error rollback. |
| Separately allocated native storage | Can retain a failed handle through asynchronous cleanup, but changes raw storage, ownership, reconstruction, and allocation costs. |
| Fix in libuv | Moving protocol discovery before `uv__handle_init` removes this dependency. This is the preferred upstream resolution; uvpp does not bundle or patch consumers' libuv. |
| Current uvpp rollback | Synchronously removes only the inactive, uncommitted registration described below, preserves application `data`, and clears native storage. No loop driving or cleanup allocation. |

No upstream issue or patch has been submitted by this task. An upstream ordering
fix would allow removing the workaround for the affected newer versions while
retaining compatibility handling for older ones.

## Audited versions

The following **official source tags** were reviewed, including
`src/win/poll.c`, `src/uv-common.h`, `include/uv.h`, and `src/unix/poll.c`.
These are source-audit results, distinct from native runtime validation.

| Tags | Registration queue representation |
| --- | --- |
| [1.44.2](https://github.com/libuv/libuv/blob/v1.44.2/src/win/poll.c), [1.45.0](https://github.com/libuv/libuv/blob/v1.45.0/src/win/poll.c) | `void *handle_queue[2]`; `QUEUE_INSERT_TAIL` |
| [1.46.0](https://github.com/libuv/libuv/blob/v1.46.0/src/win/poll.c), [1.47.0](https://github.com/libuv/libuv/blob/v1.47.0/src/win/poll.c), [1.48.0](https://github.com/libuv/libuv/blob/v1.48.0/src/win/poll.c) | `struct uv__queue`; `uv__queue_insert_tail` |
| [1.49.0](https://github.com/libuv/libuv/blob/v1.49.0/src/win/poll.c), [1.49.1](https://github.com/libuv/libuv/blob/v1.49.1/src/win/poll.c), [1.49.2](https://github.com/libuv/libuv/blob/v1.49.2/src/win/poll.c) | Named queue, same insertion/removal invariants |
| [1.50.0](https://github.com/libuv/libuv/blob/v1.50.0/src/win/poll.c), [1.51.0](https://github.com/libuv/libuv/blob/v1.51.0/src/win/poll.c) | Named queue, same insertion/removal invariants |
| [1.52.0](https://github.com/libuv/libuv/blob/v1.52.0/src/win/poll.c), [1.52.1](https://github.com/libuv/libuv/blob/v1.52.1/src/win/poll.c), [1.53.0](https://github.com/libuv/libuv/blob/v1.53.0/src/win/poll.c) | Named queue, same insertion/removal invariants |

The common registration macro initializes `loop`, `type`, reference flags, the
registration link, and platform bookkeeping; it does not activate the handle or
increment the active-handle count. Compare
[1.44.2 common init](https://github.com/libuv/libuv/blob/v1.44.2/src/uv-common.h)
and [1.53.0 common init](https://github.com/libuv/libuv/blob/v1.53.0/src/uv-common.h).
The queue representation change is visible between
[1.45.0 headers](https://github.com/libuv/libuv/blob/v1.45.0/include/uv.h) and
[1.46.0 headers](https://github.com/libuv/libuv/blob/v1.46.0/include/uv.h).

The 1.44.2 entry is a historical source audit, not a claim that the complete
uvpp API builds against it: the current loop wrapper uses metrics APIs introduced
in 1.45.0. The oldest pinned executable test therefore uses 1.45.0.

This is **not a blanket guarantee for all libuv 1.x versions**, future releases,
vendor patches, or libuv 2. The implementation detects the two audited queue
shapes; that compile-time check alone cannot validate native backend semantics.
Use headers matching the linked library, and review the native failure ordering
when updating or patching libuv. Normal CI tests the installed dependency; the
pinned native jobs described below exercise both audited queue representations.

## Backend and failure scope

| Backend | Failure ordering and rollback applicability |
| --- | --- |
| Windows `src/win/poll.c`, native socket overload | `ioctlsocket(FIONBIO)` may fail before registration: no rollback. After optional `SIO_BASE_HANDLE` resolution, common registration occurs; `getsockopt(SO_PROTOCOL_INFOW)` may then fail: registration rollback applies. |
| Windows integer descriptor overload | Delegates to the same socket initializer after CRT-descriptor conversion; the same native late failure boundary applies. uvpp rejects negative descriptors before native entry. |
| Windows fast AFD polling vs slow worker polling | Backend selection, peer-socket discovery and request initialization occur **after** the protocol query. The failure is therefore common to both possible modes, not an error from either running backend. |
| Unix `src/unix/poll.c` in the audited tags | Descriptor checking and nonblocking setup precede `uv__handle_init`. Native init failure does not leave a registration; no queue rollback occurs. This does not generalize to unreviewed platform implementations. |

At the audited late Windows failure:

1. The handle is registered, inactive, and not closing.
2. No poll request has been initialized/submitted and no poll-owned resource has
   been acquired. Fast/slow selection has not occurred.
3. Removing its queue link restores the loop's registration list; no active count
   or close/endgame queue needs adjustment.
4. Clearing native storage and restoring application `data` makes the sentinel
   null again. External socket nonblocking changes are not reverted.

The helper is not valid for a successful public init, an active poll, queued
requests, or a closing handle. Public repeat init always returns `UV_EALREADY`.
Any loop-owned infrastructure belongs to the loop; it must not be freed as a
poll-owned resource.

## Native Windows failure tests

[`test-raw-poll-windows.cpp`](../../tests/test-raw-poll-windows.cpp) executes the
**actual linked `uv_poll_init_socket` binary**. A scoped Windows import-table hook
in the module containing libuv fails its Winsock `SO_PROTOCOL_INFOW` query with
`WSAEINVAL`. Inside that call, before returning the error to libuv, the test uses
`uv_walk` to verify that the expected poll is already registered, inactive, and
bound to the expected loop. The rest of native init/error propagation is real.
This is Winsock-call fault injection, not a naturally occurring provider failure.
It does not replace `uv_poll_init_socket` or inject an error at `finish_poll_init`.

Two cases then verify:

- status init removes registration, preserves neighboring handles/native address/
  application data, and permits retry after the hook is removed;
- both loop and loop-view throwing constructors remove their native registration
  before raising `uv::error`, without leaving a live loop or dangling storage.

The hook delegates unrelated calls and other threads to the real Winsock function,
and restores the import table on scope exit. It supports the test executable's
static libuv or a separate libuv DLL. Failure to install the hook fails the test;
it does not silently skip the native error path. Runtime/header versions are
recorded in GoogleTest XML, and the test checks that they match.

The normal Windows suite includes these tests. The workflow also defines two
native MSVC jobs using [`tests/windows-poll-native`](../../tests/windows-poll-native/CMakeLists.txt):

| Official libuv tag | Linkage | Purpose |
| --- | --- | --- |
| `v1.45.0` | Static (`uv_a`) | Pointer-array queue and interception in the executable |
| `v1.53.0` | Shared (`uv`) | Named queue and interception inside the native DLL |

These jobs are configured for CI; adding them is not evidence that they have run
successfully. Linux tests still retain the portable synthetic rollback-boundary
case for loop integrity. Cross-compiling Windows syntax likewise does not replace
native MSVC execution or the actual Winsock error test.
