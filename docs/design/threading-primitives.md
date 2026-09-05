# Threading Primitives

Status: implemented in v2.

The wrappers in `include/uvpp/threading/primitives.hpp` expose libuv thread and
synchronization primitives. They synchronize application state and do not make
loop, handle, request, callback-slot, or `user_data` access generally thread-safe.
See the [thread-safety contract](thread-safety.md) and
[user guide](../user/threading.md).

## Scope and Ownership

The current types are `thread`, `mutex`, `recursive_mutex`, `rwlock`, `semaphore`,
`condition_variable`, `barrier`, `thread_key`, and `once`. Native access is explicit
through `native()`. Copying and moving are disabled to preserve native identity.

Synchronization primitives use RAII for native initialization and destruction;
initialization errors throw `uv::error` where libuv reports them. Application code
must ensure that no thread still uses a primitive when it is destroyed. There is
no hidden reference counting or waiting to repair invalid ownership.

These are direct libuv interop wrappers. Standard C++ synchronization facilities
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
unexpected native failures. This is the documented synchronization exception to
v2's general non-throwing `try_*` convention.

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
