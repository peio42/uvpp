# Processes

`uv::process` is an experimental v3 owner for one child process. Construction
calls `uv_spawn()` synchronously: a launch failure throws `uv::error` from the
constructor, while a successful owner already represents a running child.

```cpp
#include <uvpp/handles/process.hpp>

uv::process_options options;
options.file = "/bin/sh";
options.arguments = {"-c", "exit 0"};

uv::process child{loop, options};
auto exit = co_await child.wait();
co_await child.close();
```

`arguments` excludes `argv[0]`; the owner puts `file` first in the vector passed
to libuv. `cwd` is optional. This slice inherits the parent environment and does
not expose stdio, environment, uid/gid, flags, or pipe configuration yet.

## Exit and cancellation

`wait()` has one active coroutine waiter. A second concurrent wait throws
`UV_EBUSY`; `co_await uv::ops::wait(child)` reports it as
`result<process_exit>{UV_EBUSY}`. Exit is terminal and retained: once libuv has
called its exit callback, later waits return the same `process_exit` immediately.
An exit status different from zero and signal termination are successful `wait()`
outcomes represented by `process_exit::status` and `process_exit::signal`.

Stopping the waiting task cancels only that waiter with `UV_ECANCELED`. It does
not signal or close the child; another task can subsequently wait for the exit.
Use `kill(signum)` to send a signal. `uv::ops::kill(child, signum)` returns the
matching immediate `uv::status` result.

## Closing and destruction

`close()` and `request_close()` are permitted only after the exit callback. Before
that point they report `UV_EBUSY`, leaving the process untouched. This prevents a
Unix close from abandoning reaping and creating a zombie process.

`co_await child.close()` waits for libuv's native close callback. The close is
non-cancellable and repeated close waits join the same transition. When a process
owner is destroyed before exit, uvpp retains its native state, observes exit, then
closes it automatically. It does not kill the child or drive a nested event loop;
the application must keep driving the associated loop. A child that never exits
therefore keeps that state alive. Process supervision, timeouts and
`resource_scope` integration remain future work.

`native()` and `native_handle()` expose borrowed libuv pointers. They do not grant
an independent close path; bypassing the owner lifecycle is unsupported. Native
`data` belongs to uvpp and must not be overwritten, and uvpp-owned callbacks must
not be replaced. Moving the owner preserves its native storage and callback state.
The separate `uv::raw::process` wrapper leaves `data` available to application code.

## Raw process handles

`uv::raw::process` owns address-stable native storage and can be default
constructed before starting a child. Its `spawn(loop, options, callback)` and
`spawn_static<Callback>(loop, options)` operations return `uv::status`. Runtime
callbacks may be empty. The exit result contains the native exit status and
signal; a nonzero child exit status is not a spawn error. The corresponding
throwing constructors are still available.

`spawn` is also the native initialization operation; there is no separate
`init()`. `initialized()` is false before native submission. Once `uv_spawn()`
has been called, **even if it returns an error**, the native process handle must
be closed and kept alive until its close callback. In that state `initialized()`
is true and another `spawn()` returns `UV_EALREADY`; create another wrapper for
another attempt. A native failure does not necessarily mean no child exists:
libuv can start the child and then fail while setting up stdio. In that case
`active()` and `pid()` expose the native state, and the exit callback remains
installed; wait for exit before closing. Before native entry, C++ option and
callback preparation can allocate and throw without initializing the handle. An oversized stdio vector is
rejected with `UV_E2BIG` before native entry. For a throwing constructor, uvpp
retains failed native storage until its internal close callback. If a child is
already active, it retains the storage through exit before closing; the caller
must continue driving the loop. This storage is separately allocated even for a
raw process, and preparing the argument and environment pointer arrays can also
allocate. No nested event loop is run.

The exit callback runs on the loop thread. Its stored runtime callable is
released from the handle before invocation. Native `data` remains application
owned, and `native()` and `native_handle()` are explicit borrows. `kill(signum)`
and the static `process::kill(pid, signum)` return native statuses; `fileno()`
and buffer-size queries return typed results. Handle operations before spawn or
after close starts report `UV_EBADF` where an error can be returned. `pid()`
returns zero until a child has actually spawned and otherwise observes the native
PID, including after exit.

The caller retains the handle through native close completion. Closing a running
child follows libuv's native behavior: on Unix it can leave a zombie which the
caller must reap. Normally wait for exit before closing. A close does not kill the
child. The options' stdio handles must satisfy libuv's own lifetime and setup
requirements. See [libuv process handles](https://docs.libuv.org/en/v1.x/process.html).
