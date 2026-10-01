# Thread Safety

## Baseline

uvpp follows libuv's threading model. Unless a function is explicitly documented as thread-safe by libuv and by uvpp, `uv` wrapper objects must be accessed from the thread that owns the associated `uv_loop_t`.

The low-level wrappers do not add locks, atomics, or cross-thread lifetime management.

## Object Access

Rules:

- `loop`, handles, and requests are not generally thread-safe;
- callback slots must be installed and replaced from the loop-owning thread;
- `uv::raw` `user_data<T>()` is an application pointer and has no synchronization;
- high-level native `data` may belong to uvpp and must not be overwritten through
  a borrowing native accessor;
- destroying wrappers from another thread while the loop may reference them is a lifetime error;
- native access through `native()` follows the same thread-safety rules as the underlying libuv object.

This keeps the core wrapper zero-overhead and avoids implying guarantees libuv does not provide.

## Coroutine affinity

Tasks inherit their root loop. Current owner awaits and scope operations check
that context; same-loop join and loop-thread stop are required. These facilities
do not make external-thread owner access safe. See [coroutines](../user/coroutines.md).

## Cross-Thread Communication

The spellings below describe current low-level implementation machinery. Its raw
namespace migration remains unfinished; it is not a second high-level v3 owner API.


Cross-thread work should use libuv mechanisms designed for it, primarily `uv::async`.

The supported shape is:

```cpp
uv::async wakeup(loop, callback);

// other thread
wakeup.send();
```

`async::send()` is the cross-thread entry point. The callback still runs on the loop thread.

Only the specific cross-thread entry points documented by libuv and by this project should be called from other threads. Installing or replacing the async callback, closing the handle, changing `user_data`, and destroying the wrapper remain loop-thread operations.

If another thread publishes data before calling `send()`, synchronization is still the application's responsibility. The wrapper does not add memory fences or queues around `uv_async_send`.

`uv::queue_work()` is a separate thread-pool mechanism: its work callback runs
on a libuv worker thread, and its after-work callback runs on the loop thread.
The work callback must follow the same rule as any other external thread: it
must not access loop-owned handles or requests except through APIs documented as
cross-thread safe. Shared application state still needs application-provided
synchronization.

## Higher-Level Scheduling

`uv::loop_posting` is an experimental, explicit cross-thread publication
component. Its owner is created and closed on the loop thread; its copyable
`loop_posting_endpoint` is the producer capability and exposes no native handle
or loop access. `endpoint.post(...)` may be called from another thread and
executes accepted work on the loop thread. `uv::ops::post(endpoint, ...)` reports
queue-full and closed rejections as a `uv::status`.

The component owns its synchronization, bounded queue, and `uv_async_t`; no lock
or cross-thread lifetime management is added to ordinary handles, requests, tasks,
or resource owners. Close is loop-thread-affine: it rejects new work, drains every
accepted callable, and then closes the wakeup handle. See the
[loop scheduling proposal](../proposals/007-loop-scheduling.md) for the remaining
continuation-policy and validation work.

## Threading Primitives

Wrappers for libuv thread and synchronization primitives synchronize
application-owned state. They do not change the baseline rule that `loop`,
handles, requests, callback slots, and `user_data` are not generally
thread-safe.

Their implemented design is recorded in
[Threading primitives strategy](threading-primitives.md). The important
boundary is that adding `uv::mutex`, `uv::thread`, or similar types does not add
hidden locking to existing uvpp objects.
