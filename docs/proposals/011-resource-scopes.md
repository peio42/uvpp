# Resource Scopes

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Dependencies: [002 — Asynchronous ownership](002-async-ownership.md),
[003 — Cancellation and task scopes](003-cancellation-and-task-scopes.md), and
[004 — Shared operation state](004-operation-state.md).

Target: v3 exploration. This proposal does not introduce a current public API.

## Motivation

`task_scope` owns the execution and frames of related child tasks. It cannot, by
itself, make owner destruction await native close completion: a task may finish
while its `tcp_connection` is still closing, and exceptional cleanup needs a
surviving asynchronous owner. Combining task execution and resource cleanup in
one type would conflate two independent lifetimes and make either policy hard to
use without the other.

## Proposed direction

Introduce a distinct, non-movable `uv::co::resource_scope` associated with one
`uv::loop`. It owns registrations of high-level resource owners or cleanup tokens.
Its asynchronous exit starts required closes, waits for each native close
completion, and releases its registrations only after that completion. It neither
starts nor owns coroutine frames.

`task_scope` remains responsible for cold-task startup, cooperative stop,
fail-fast child failure, and task join. The two scopes compose explicitly. A
server-shaped operation therefore has two boundaries:

```cpp
uv::co::task<void> serve(uv::loop &loop, uv::tcp_listener &listener) {
  uv::co::task_scope tasks{loop};
  uv::co::resource_scope resources{loop}; // proposed

  while (...) {
    auto connection = co_await listener.accept();
    tasks.spawn(handle(resources.adopt(std::move(connection))));
  }

  co_await tasks.join();
  co_await resources.close();
}
```

This is illustrative only. In particular, `adopt()` and `close()` are not
committed spellings. The ordering is contractual: request task stop when the
operation fails or shuts down, join task execution, initiate/await owner cleanup,
then release scope storage. A selected cleanup policy may start close earlier, but
it must retain all native and borrowed-operation storage until real completion.

If a task and cleanup both fail, preserve the original task failure and report
cleanup failures through a specified secondary channel; never lose either
silently. Cross-loop registration is rejected deterministically. Resource-scope
destruction before asynchronous exit is an explicit contract violation until a
retention fallback has been designed and validated.

## Implementation and validation gates

Do not implement this scope before the generic internal close-completion primitive
exists for each adopted owner. The implementation must prove release-before-user-
resume for close completion, support exception paths, and avoid nested loop runs.
Validate normal exit, exception during a handler, fail-fast cancellation, multiple
close completions, cleanup failure, active borrowed I/O, owner moves, and loop
shutdown. Run the resulting paths under address and undefined-behavior sanitizers.
