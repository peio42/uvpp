# Threading Primitives

uvpp exposes libuv's thread and synchronization primitives for applications
that want to stay within the libuv portability layer.

These wrappers synchronize application-owned state. They do not make `uv::loop`,
handles, requests, callback slots, or `user_data` generally thread-safe.

## Threads

`uv::thread` starts a native libuv thread from a callable:

```cpp
uv::thread worker{[] {
  do_work();
}};

worker.join();
```

The wrapper is non-copyable and non-movable. Like `std::thread`, destroying a
joinable `uv::thread` calls `std::terminate()`. Call `join()` explicitly before
the object is destroyed.

When compiling against libuv 1.50.0 or newer, `UVPP_HAS_THREAD_DETACH` is true
and `uv::thread::detach()` is also available.

User callbacks are invoked behind uvpp's exception boundary. If a callback
throws out of the thread entry point, uvpp terminates the program instead of
letting the exception escape into libuv's C callback.

Use `uv::thread::self()` and `uv::thread::equal()` for raw libuv thread identity
interop.

## Mutexes

`uv::mutex` uses the standard mutex vocabulary:

```cpp
uv::mutex lock;

{
  std::lock_guard<uv::mutex> guard{lock};
  shared_state.update();
}
```

`lock()` blocks, `try_lock()` attempts to acquire without blocking and returns
`bool`, and `unlock()` releases the mutex. This shape works with
`std::lock_guard` and `std::unique_lock`.

`uv::recursive_mutex` exposes the same API but uses libuv's recursive mutex
initialization.

## Read-Write Locks

`uv::rwlock` keeps read and write ownership explicit:

```cpp
uv::rwlock lock;

lock.lock_read();
read_shared_state();
lock.unlock_read();

lock.lock_write();
write_shared_state();
lock.unlock_write();
```

The non-blocking variants are `try_lock_read()` and `try_lock_write()`.

## Semaphores, Condition Variables, And Barriers

`uv::semaphore` exposes `post()`, `wait()`, and `try_wait()`.

`uv::condition_variable` exposes `signal()`, `broadcast()`, `wait(mutex)`, and
`wait_for(mutex, duration)`. Timed waits use `std::chrono` durations and return
`false` on timeout.

The passed `uv::mutex` must be locked when calling `wait()` or `wait_for()`.
These waits may wake spuriously, so callers should re-check their predicate
after every wakeup.

`uv::barrier::wait()` returns `uv::barrier_wait_result::passed` or
`uv::barrier_wait_result::serial_thread`.

## Thread-Local Keys And Once

`uv::thread_key` stores a raw thread-local pointer:

```cpp
uv::thread_key key;
key.set(&state);

auto *state_ptr = key.get<state_type>();
```

`uv::once` wraps `uv_once_t`:

```cpp
uv::once once;
once.run_static<initialize_global_state>();
```

The runtime `run()` overload accepts a plain `void (*)()` function pointer,
matching libuv's native callback shape.
