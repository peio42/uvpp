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
an independent close path; bypassing the owner lifecycle is unsupported.
