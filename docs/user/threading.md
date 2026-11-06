# Threading primitives

The synchronous thread and synchronization owners are in `uv`: `thread`,
`mutex`, `recursive_mutex`, `rwlock`, `semaphore`, `condition_variable`,
`barrier`, `thread_key` and `once`. Include `<uvpp/threading/primitives.hpp>`.
They are separate from libuv's shared thread pool and its `uv::raw::queue_work`
requests. They do not make event-loop objects thread-safe.

Native initialization failures throw `uv::error`; C++ allocations can throw
separately. Synchronization owners are non-copyable and non-movable and destroy
their native resource on destruction. The caller must ensure no threads are
using or waiting on the resource before destruction. Native error paths that
libuv aborts are not converted into exceptions. `native()` is an explicit borrow.

`mutex` and `recursive_mutex` work with standard lock guards. `rwlock` has
separate read and write operations. The `try_*` methods return false for native
contention and throw on other reported errors. Native recursion and ownership
rules vary by platform; do not depend on reacquiring a nonrecursive lock. A
semaphore's `try_wait()` returns false when no permit is available. Semaphore
and barrier counts accept integral values in the native `unsigned int` range;
negative or out-of-range counts throw `UV_EINVAL` before conversion. A barrier
requires a positive participant count. Platforms can impose lower native limits.

A condition wait requires its mutex to be held. Wait in a predicate loop to
handle spurious wakeups. `wait_for` uses a relative chrono duration: negative
values become zero, fractional nanoseconds truncate toward zero, positive
out-of-range values and infinity saturate to the largest native nanosecond count,
and NaN throws `UV_EINVAL`. Floating-point representation can limit precision.
Libuv has backend limitations for very large native timeouts: Linux libuv 1.51.0
and 1.53.1-dev were observed to time out immediately at the maximum count; source
inspection also suggests a Windows conversion issue that has not been tested on
Windows. Use a suitable finite timeout and account for the linked libuv version.

`thread` default construction is non-joinable. Construction with a callable or
`start()` allocates a decayed callable and starts a native thread. The callable
may begin or even finish before `start()` returns. Its state belongs to the
worker after native creation succeeds and is destroyed after it returns, including
after explicit `detach()` when available. Failed creation destroys the prepared
state. Captured references remain the application's responsibility. A callable
exception terminates the process; exceptions do not escape the C entry point.

`join()` waits and clears joinability on success; `detach()` is available when
`UVPP_HAS_THREAD_DETACH` is enabled. `start()` on a joinable thread throws
`UV_EBUSY`; `join()` or `detach()` on a non-joinable thread throws `UV_EINVAL`.
Destroying a joinable `thread` terminates the process: it neither joins nor
detaches automatically. Thread object operations need external synchronization.

`thread_key` owns the key, not the per-thread pointers stored in it; preserve the
pointed-to objects yourself and stop using the key before destruction. `once`
executes a non-null function pointer or `run_static<Callback>()` at most once;
the static callback must not let exceptions escape. Blocking operations block
the calling OS thread and do not drive an event loop.
