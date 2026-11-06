# Threading Primitives

Status: implemented; classification in `uv` is final.

The wrappers in `include/uvpp/threading/primitives.hpp` expose libuv thread and
synchronization primitives. They synchronize application state and do not make
loop, handle, request, callback-slot, or `user_data` access generally thread-safe.
See the [thread-safety contract](thread-safety.md).

## Scope and Ownership

The current types are `thread`, `mutex`, `recursive_mutex`, `rwlock`, `semaphore`,
`condition_variable`, `barrier`, `thread_key`, and `once`. Native access is explicit
through `native()`. Copying and moving are disabled to preserve native identity.

Synchronization primitives use RAII for native initialization and destruction;
initialization errors throw `uv::error` where libuv reports them. Application code
must ensure that no thread still uses a primitive when it is destroyed. There is
no hidden reference counting or waiting to repair invalid ownership.

These synchronous, owning primitives are classified in `uv`, not `uv::raw`:
they do not use event-loop handle/request lifecycles. `thread` owns a native thread
and its entry state rather than an asynchronous libuv request. Standard C++ synchronization facilities
remain valid alternatives; using the wrappers does not change the event-loop model.
Future scheduling facilities are tracked in the
[loop scheduling proposal](../proposals/007-loop-scheduling.md).

## Thread Ownership

Default construction creates a non-joinable thread wrapper. Callable construction
or `start(callback)` starts execution. Starting an already-joinable thread reports
`UV_EBUSY`. `join()` waits for completion and clears joinability; destroying a
joinable thread calls `std::terminate()`. There is no implicit join or detach.

`detach()` is available under `UVPP_HAS_THREAD_DETACH`; the native operation must
succeed before joinability is cleared. `self()` and `equal()` provide native thread
identity operations.

Starting a thread allocates callable-owned state, retained through successful
submission and transferred to the thread entry point. Submission failure releases
that state. The entry point owns it until callback completion. Callback exceptions
are contained by `detail::invoke_callback` and terminate the process.

## Locks and Waiting

`mutex` and `recursive_mutex` expose `lock()`, `try_lock()`, and `unlock()` for use
with standard lock guards. `rwlock` exposes separate read/write lock operations.
The `try_*` lock operations return false for ordinary contention and throw for
unexpected native failures. These are actual attempt semantics, consistent with the v3 `try_*` naming rule.

`semaphore` exposes `post()`, `wait()`, and `try_wait()`. Condition variables expose
signal, broadcast, wait with a `mutex&`, and a chrono-based timed wait. Callers
must hold the mutex and check their predicate in a loop to handle spurious wakeups.
Barrier waiting returns a typed result identifying the serial participant.

Blocking waits block the calling thread, including the loop thread if called there.
They do not suspend a coroutine or dispatch event-loop work.

## Thread-Local Keys and Once

`thread_key` owns the native key, not application values stored through it. It
provides raw and typed getters, pointer setters, and `clear()`; the typed accessors
are cast conveniences rather than runtime type checking.

`once` uses the native static initializer and exposes a `noexcept` function-pointer
form and `run_static<Callback>()`. It does not store arbitrary captured callables.
The static trampoline retains the callback exception boundary.

Contention tests use a separate contender thread while the owning thread keeps
the lock held, then join before releasing it. Tests do not infer busy behavior
from same-thread reacquisition: Windows mutexes are recursive, while BSD
read/write lock reacquisition may produce `EDEADLK`, which libuv aborts on.

## Count and timeout conversion

Semaphore and barrier constructors accept integral counts representable by the
native `unsigned int`; negative or out-of-range values throw `uv::error` with
`UV_EINVAL` before conversion. A zero barrier count is rejected by libuv. Native
platforms can impose lower limits, reported through their native initialization
status. Boolean and floating-point counts are not accepted.

Condition timeouts convert to nanoseconds, truncating fractional nanoseconds
toward zero. Negative values become zero, positive overflow and infinity saturate
to `UINT64_MAX`, and NaN throws `UV_EINVAL` before native entry. Floating-point
precision limits apply. Native `uv_cond_timedwait` remains responsible for its
platform conversion; uvpp does not mask native defects.

On Linux, a native reproducer observed a `UINT64_MAX` nanosecond timeout returning
`UV_ETIMEDOUT` immediately with libuv 1.51.0 and 1.53.1-dev (commit `2b4b918d`):
the Unix backend adds the relative timeout to a monotonic timestamp in `uint64_t`
and wraps. On Windows, source inspection at that commit shows a conversion to
`DWORD` milliseconds which can exceed the destination range; this is a suspected
limitation, not yet verified by running on Windows. No matching upstream report
was identified in the search; the native reproducer should be reported upstream.
The wrapper does not retry, clamp the native range, or fabricate a result.

Minimal native reproducer (Linux; no uvpp calls):

```c
#include <stdint.h>
#include <stdio.h>
#include <uv.h>

int main(void) {
  uv_cond_t condition;
  uv_mutex_t mutex;
  uv_cond_init(&condition);
  uv_mutex_init(&mutex);
  uv_mutex_lock(&mutex);
  uint64_t start = uv_hrtime();
  int status = uv_cond_timedwait(&condition, &mutex, UINT64_MAX);
  printf("%s: status=%d, elapsed_ms=%llu\n", uv_version_string(), status,
         (unsigned long long)((uv_hrtime() - start) / 1000000));
  uv_mutex_unlock(&mutex);
  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&condition);
}
```

With no signal, a roughly 584-year relative wait is expected, while the observed
result was `UV_ETIMEDOUT` within milliseconds. Source for the reviewed Unix and
Windows implementations is available at [libuv commit 2b4b918d, Unix](https://github.com/libuv/libuv/blob/2b4b918d/src/unix/thread.c)
and [Windows](https://github.com/libuv/libuv/blob/2b4b918d/src/win/thread.c).
