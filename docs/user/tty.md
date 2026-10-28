# Raw terminal streams

Include `<uvpp/handles/tty.hpp>` or `<uvpp/uv.hpp>`. `uv::raw::tty` is an
address-stable, caller-owned libuv terminal handle on the shared `uv::loop`; it is
neither copyable nor movable. Native `data` belongs to the application. The
common `tty_mode`, `terminal_size` and `tty_vterm_state` vocabulary stays in `uv`.

```cpp
uv::raw::tty terminal;
auto initialized = terminal.init(loop, terminal_fd, true);
if (!initialized) return;
auto size = terminal.size(); // uv::result<uv::terminal_size>
// Retain terminal until the asynchronous close completes.
terminal.close();
```

`init(loop_or_view, uv_file, bool readable)` returns `uv::status` and is
`noexcept`. Matching loop/view constructors call `init(...).throw_if_error()`.
Libuv currently ignores `readable`: native descriptor access determines whether a
terminal can read or write. `initialized()` observes the native loop pointer.
Repeat init returns `UV_EALREADY`, including during or after close. An unsuccessful
native init leaves uninitialized, reusable storage and preserves application
`data`; no close is needed for that failed attempt. Pre-init close is inert.

The descriptor contract is native and platform-dependent. On Unix libuv may reopen a
TTY slave and replace the supplied descriptor with a duplicate before using a
separate descriptor internally; on fallback it may use the supplied descriptor
itself. On Windows it duplicates standard descriptors 0–2 and normally takes
ownership of other descriptors. Close releases the descriptor or native handle
owned by libuv. Consult the linked libuv version's contract before closing or
reusing the supplied descriptor, and avoid racing descriptor operations with init
or close. Init can change descriptor flags or identity as part of native setup;
a failed native operation does not promise rollback of every descriptor side effect.
In inspected Windows libuv 1.45/1.48/1.53 paths, failure after duplicating fd
0–2 can leave that native duplicate open; the wrapper cannot recover it from
the failed native handle.

`set_mode(tty_mode)` returns `status`; invalid enum values return `UV_EINVAL`
before native entry. `size()` returns
`result<terminal_size>` and preserves width and height. Both return `UV_EBADF`
before init or while closing; supported modes and size query failures depend on the
native terminal/backend. `tty_mode::raw_vt` is available under
`UVPP_HAS_TTY_RAW_VT` (libuv 1.51 or newer). On Windows a native mode change can
stop and restart active reads; failure during that process can leave native reads
stopped. Call `read_stop()`, restart or close to retire the selected callbacks.
Normal close and runtime/static close callbacks use the
shared stream lifecycle. Keep the handle alive through native close completion.

`tty` inherits the [raw stream](networking/raw-stream.md) read subscription,
write, shutdown, `blocking` and close protocols. Actual operation support remains
native: terminals do not listen or accept connections, and mode/descriptor
configuration determines whether they can read or write. `read_result` retains
its borrowed buffer, byte count, status and EOF. `write_request` is separately
owned, and payload bytes remain borrowed through completion. Runtime callback
preparation may allocate or throw independently of native statuses.

## Process-wide terminal state

`uv::raw::tty_reset_mode() -> status` wraps libuv's global reset; it has no handle
or initialization precondition. Under `UVPP_HAS_TTY_VTERM_STATE`,
`uv::raw::tty_set_vterm_state(state)` and
`uv::raw::tty_get_vterm_state() -> result<tty_vterm_state>` expose libuv's
process-wide virtual-terminal setting and query. Availability and behavior remain
backend-dependent; a query may return `UV_ENOTSUP`. On Windows, initialize libuv
first (for example by constructing a `uv::loop`) before calling the vterm setter
or getter: the inspected libuv 1.45/1.48/1.53 Windows implementations initialize
their shared semaphore during library/loop initialization, not in those functions.
No TTY handle is required. These functions do not imply that terminal settings are
local to a handle; uvpp does not implicitly create a loop.
