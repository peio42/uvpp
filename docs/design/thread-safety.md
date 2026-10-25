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

`uv::raw::async` is the caller-owned notification primitive. Its namespace and
error-policy migration is implemented; it is distinct from high-level posting.


Cross-thread work should use libuv mechanisms designed for it, primarily `uv::raw::async`.

The supported shape is:

```cpp
uv::raw::async wakeup(loop, callback);

// other thread
auto submitted = wakeup.send(); // uv::status; handle submitted.error() if needed.
```

`async::send()` is the cross-thread entry point. The callback still runs on the loop thread.

Only the specific cross-thread entry points documented by libuv and by this project should be called from other threads. Installing or replacing the async callback, closing the handle, changing `user_data`, and destroying the wrapper remain loop-thread operations.

Publish shared data using application-owned synchronization such as a queue mutex.
The wrapper adds no memory fences or queue around `uv_async_send`. Native memory
ordering is version-dependent: libuv documents stronger send/callback sequential
consistency starting with 1.53.0; the raw wrapper does not backport that guarantee.
See [native async memory ordering](https://docs.libuv.org/en/v1.x/async.html).

Complete `async::init()` before publishing the handle to producer threads and
never overlap initialization attempts with sends. `send()` checks the native
`loop` pointer for pre-init `UV_EBADF`; it remains unchanged after successful init,
including through close. This non-atomic observation is not synchronization with
init or a lifetime guard. The other initialization/state operations are loop-thread
only.

Before close, prevent new sends and wait for every in-flight producer send to
return. A concurrent send/close or send after close is outside this raw protocol.
`send()` does not read `closing()` because that would race with loop-thread state
without retaining native storage. Callback setters do check closing on the loop
thread and return `UV_EBADF`; send has no corresponding admission guarantee.
`set_callback({})` clears user delivery without stopping the native handle.
See [raw async notifications](../user/async.md) for callback and close lifetimes.

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

### Posting synchronization and retention

Admission, queue mutation, native send, and the transition to `uv_close()` share
one mutex. Acceptance linearizes after insertion and successful send while holding
that lock; unsuccessful send rolls the insertion back. Once admission is sealed,
producers do not touch native storage. The queue, rather than a count of async
callbacks, accounts for accepted work because native wakeups coalesce.

The loop extracts at most the configured drain budget, then invokes and destroys
captures outside the lock. An active-batch count prevents close during either
invocation or capture destruction. A reusable batch vector is reserved before
native initialization, so drain bookkeeping needs no vector growth in a C callback.
Only loop driving consumes the batch; nested `loop.run()` remains unsupported.

Owner and producer endpoints explicitly share stable posting state. Native close
completion takes a temporary strong reference, marks closed and detaches waiters
under the lock, then clears the failure handler and resumes waiters outside it.
This retains callback storage if a resumed task releases the owner and awaiters.
Clearing the handler also breaks any retained endpoint capture cycle at close.
Endpoints may retain the inert state after the owner and loop are gone.

Costs are explicit: shared state/control block, one type-erased allocation per
prepared callable, deque growth during submission, batch storage reserved at
construction, and possible close-waiter or failure-handler allocation. Rejected
submissions currently prepare their callable before checking admission. No general
allocation-free posting or latency claim is made. Capacity bounds waiting work,
not payload bytes or transient preparation by concurrent producers; the extracted
batch is bounded separately.

`make test-tsan-posting` builds just the posting tests with ThreadSanitizer in a
separate directory. It excludes death tests, which fork. Ordinary full compiler
suites and ASan/UBSan also exercise the lifecycle and termination contracts.
The focused target uses `timeout` to bound test execution at 120 seconds; it
requires that utility alongside a supported TSan runtime. Other test targets can
opt into the same bound with `TEST_RUNNER='timeout 120s'`.

## Threading Primitives

Wrappers for libuv thread and synchronization primitives synchronize
application-owned state. They do not change the baseline rule that `loop`,
handles, requests, callback slots, and `user_data` are not generally
thread-safe.

Their implemented design is recorded in
[Threading primitives strategy](threading-primitives.md). The important
boundary is that adding `uv::mutex`, `uv::thread`, or similar types does not add
hidden locking to existing uvpp objects.
