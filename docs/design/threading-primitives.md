# Threading Primitives Strategy

Status: design direction for a future v2 implementation.

This document records the intended shape for wrappers around libuv's threading
and synchronization primitives. These wrappers should extend uvpp's low-level
libuv coverage without changing the existing event-loop and handle contracts.

## Scope

The intended v2 scope is:

- `uv_thread_t`;
- `uv_mutex_t`;
- `uv_rwlock_t`;
- `uv_sem_t`;
- `uv_cond_t`;
- `uv_barrier_t`;
- `uv_key_t`;
- `uv_once`.

These types are useful for callers that want to stay inside the libuv
cross-platform primitive set, and for future higher-level uvpp facilities such
as synchronized queues, executor-style APIs, or channels.

They are not required before coroutine awaitables can exist. Coroutine support
should primarily resume from libuv completion callbacks on the loop thread.
Cross-thread coroutine wakeups, when needed, should be explicit and will likely
use `uv::async` plus application-owned synchronization.

## Relationship To `std`

C++20 already provides `std::thread`, `std::mutex`, condition variables,
`thread_local`, `std::call_once`, and related facilities. The libuv wrappers
should therefore be small, direct interop wrappers rather than a replacement
concurrency framework.

The main reasons to expose them are:

- parity with libuv's public surface;
- applications that choose libuv primitives for portability or ecosystem
  consistency;
- future uvpp components that need an explicit libuv-backed synchronization
  building block.

Do not use these wrappers to imply that uvpp loop, handle, or request objects
become thread-safe.

## Ownership And Lifetime

Synchronization primitive wrappers should use RAII:

- initialize the native primitive in the constructor;
- destroy it in the destructor;
- throw `uv::error` for immediate initialization failures where libuv reports
  them;
- keep raw native access explicit through `native()`.

Destroying a primitive while another thread may still use it is an application
lifetime error. The wrappers should not add hidden reference counting, waiting,
or ownership transfer to make that safe.

Like handles and requests, these wrappers should be non-copyable and
non-movable in the first v2 implementation. Even though they are not loop
handles, this keeps address stability and ownership simple. Movability can be
revisited only with a specific design that preserves native lifetime guarantees.

## Thread Wrapper

`uv::thread` is the most consequential wrapper because it owns an executing
thread rather than only a passive synchronization object.

The intended model is deliberately close to `std::thread`:

- construction starts the thread;
- `joinable()` reports whether a thread still needs a terminal ownership
  action;
- `join()` waits for completion and clears the joinable state;
- destruction of a joinable `uv::thread` calls `std::terminate()`;
- copying and moving are disabled.

Do not join implicitly in the destructor. Blocking in a destructor hides a
large lifetime decision and can deadlock user code. Do not detach implicitly
either; detached execution should be an explicit API if uvpp exposes it.

The public callback shape should prevent exceptions from escaping through the
native libuv thread entry point. If a user callback throws, the trampoline must
catch it and apply the documented failure policy for this wrapper. A simple
initial policy may be `std::terminate()`, matching the usual rule that
uncaught exceptions escaping a thread function terminate the program.

Callable-owning constructors may need to allocate state that is handed to the
native thread entry point. That is acceptable when the constructor explicitly
owns a thread operation, but it should be documented. If useful, provide a
static-callback construction form later for callers that want to avoid runtime
callable storage.

## Mutexes And Locks

`uv::mutex` should expose the standard mutex vocabulary:

```cpp
void lock();
bool try_lock();
void unlock() noexcept;
```

This allows `std::lock_guard<uv::mutex>` and `std::unique_lock<uv::mutex>` to
work with the wrapper.

`try_lock()` is an intentional exception to uvpp's general `try_*` naming rule.
For synchronization primitives, `try_lock()` has the established C++ meaning:
attempt to acquire without blocking. It should return `false` only for the
ordinary busy case. Unexpected libuv failures may still throw `uv::error` in
the primary API.

If a fully non-throwing lock attempt is needed later, add a differently named
status-returning function rather than changing `try_lock()` away from the
standard mutex contract.

## Read-Write Locks

`uv::rwlock` should keep read and write acquisition explicit:

```cpp
void lock_read();
bool try_lock_read();
void unlock_read() noexcept;

void lock_write();
bool try_lock_write();
void unlock_write() noexcept;
```

The names should make the mode visible at every call site. Separate scoped
reader/writer guard helpers may be added later, but the low-level primitive
should not require them.

## Condition Variables, Semaphores, And Barriers

`uv::condition_variable`, `uv::semaphore`, and `uv::barrier` should be thin
RAII wrappers over the corresponding libuv primitives.

Condition-variable waiting must document the mutex requirements and the
spurious-wakeup behavior of the underlying primitive. If uvpp exposes timed
wait variants, use `std::chrono` durations or time points in public APIs.

Semaphore operations should use clear blocking vocabulary. If libuv exposes a
non-blocking semaphore wait, preserve the synchronization meaning of `try_*`
for that operation instead of using it as a non-throwing marker.

Barrier waiting should expose the libuv result shape clearly, including the
special serial-thread return if that is part of the native contract.

## Thread-Local Keys And Once

`uv::thread_key` should be an RAII wrapper around `uv_key_t`. It stores and
returns raw `void*` application values, matching libuv's primitive. Typed helper
templates can be considered later, but the low-level wrapper should keep the
native contract visible.

`uv::once` should wrap `uv_once_t` and execute a function exactly once. Its API
needs special care because libuv's native callback is C-shaped and carries no
user data. Prefer a simple, explicit static-function surface first. More
ergonomic callable storage should be added only if its lifetime and exception
policy are clear.

## Documentation Requirements

When these wrappers are implemented, add user-facing documentation explaining:

- that the primitives synchronize application state, not uvpp handles;
- that loop, handle, request, callback-slot, and `user_data` thread-safety rules
  remain unchanged;
- which APIs are blocking;
- which operations can be used with standard C++ lock utilities;
- what happens when a `uv::thread` object is destroyed while still joinable.
