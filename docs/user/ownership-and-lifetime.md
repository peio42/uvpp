# Ownership and lifetime

The implemented v3 TCP/pipe connections and listeners, and UDP sockets, are
move-only owners of stable native storage. Moving an owner transfers storage;
it does not relocate the native handle. Tasks and owners borrow their `uv::loop`.
Keep that loop alive and drive it through all pending native cleanup.

## Closing owners

`co_await owner.close()` waits for the native close callback. Repeated awaits join
the same close transition. `request_close()` initiates close without waiting.
`co_await uv::ops::close(owner)` returns `uv::status` for the same operation.
Close is not stream shutdown and does not join application tasks.

Connection and UDP close reject active borrowed I/O with `UV_EBUSY`; settle that
work first. Listener close can quiesce a pending `accept()`, which receives
`UV_ECANCELED` after provisional-child cleanup. Accepted connections remain separate
owners. IPC export also retains a claim on the exported TCP owner through completion.

Direct-owner destruction starts asynchronous cleanup when its lifetime preconditions
are met. It never runs a nested loop. Destroying a connection/socket with active
borrowed I/O, or a listener with an active accept, is a terminating contract
violation. Use explicit close or resource scopes to observe completion.

`signal_source` is another move-only high-level owner. Its close transition first
stops the persistent native subscription and completes a current `next()` with
`UV_ECANCELED`; it then waits for the native close callback. Signal sources are
not yet resource-scope registrations. See [Signals](signals.md).

## Borrowing

Produce borrowed connection/socket views explicitly with `.view()`. A view is not
another close owner and does not retain native storage. Named native accessors
(`native()`, `native_handle()`, `native_stream()` where applicable) also borrow;
they do not authorize closing the handle or replacing an active callback slot.
Native `data` belongs to application code.

Operation state does not own external bytes. Follow the [buffer rules](buffers.md)
even after cancellation has been requested.

## Resource scopes

Include `<uvpp/co/resource_scope.hpp>`. A `uv::co::resource_scope` owns adopted
TCP/pipe connections and listeners, and UDP sockets. A `uv::co::task_scope` owns
child execution. Their completion boundaries are separate:

```cpp
// Inside a task bound to loop; include both scope headers and tcp_listener.hpp.
uv::co::task_scope tasks(loop);
uv::co::resource_scope resources(loop);
auto listener = resources.own(
    uv::tcp_listener{loop, uv::ipv4{"127.0.0.1", 8080}});
// Adopt connections and spawn handlers borrowing their .view() here.
co_await tasks.join();
co_await resources.finish();
```

This fragment shows normal ordering. On an exceptional path, retain the primary
exception, request stop and join child work, then finish resource cleanup before
rethrowing. Do not put a cleanup `co_await` inside a C++ catch handler; capture the
exception and await cleanup afterward.

`resources.own()` transfers the sole connection owner into `resource_scope`; the
handler receives only a `tcp_connection_view`. `task_scope::join()` finishes task
execution, while `resources.finish()` starts internal close and waits for the
actual `uv_close` callback before destroying owner storage. A view retained after
`finish()` diagnoses use instead of accessing released native state. The caller
must join tasks before calling `finish()`; cleanup with active borrowed I/O is
rejected with `std::logic_error`. This rejection is recoverable: keep the scope
alive, settle/join the borrowing tasks, then await a new `resources.finish()` task.
Earlier successful closes are not rolled back; their views are already invalid.
The retry closes only the remaining resources.

`finish()` returns a cold task borrowing the scope. Merely constructing or
abandoning that task does not seal adoption. Starting it on the associated loop
seals `own()` permanently, including after a cleanup failure. A wrong-loop await
throws before changing scope state. Overlapping cleanup attempts throw
`std::logic_error`; after successful cleanup, another await succeeds immediately
on the associated loop. Keep the scope alive through all tasks that borrow it.

Cleanup stops at the first failure and propagates that exception; it does not
aggregate errors or join application tasks. Setup/allocation failures may also
throw, and completed cleanup remains committed. Preserve any primary task failure
separately while handling cleanup errors and retrying. Cancellation does not
shorten native close completion. `resource_scope` destruction with remaining
resources is a terminating contract violation, even after a caught cleanup error;
`task_scope` likewise requires its outstanding work to be settled.

The same registration/view rules apply to pipe connections and UDP sockets;
listener registrations expose `accept()`. Views retain diagnostic bookkeeping,
not native resource lifetime. Resource scope cleanup closes listeners before
dependent owners. Generic resource registration and cleanup-error aggregation
remain proposed.
