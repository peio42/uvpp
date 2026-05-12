# Thread Safety

## Baseline

uvpp v2 follows libuv's threading model. Unless a function is explicitly documented as thread-safe by libuv and by uvpp, `uv` wrapper objects must be accessed from the thread that owns the associated `uv_loop_t`.

The low-level wrappers do not add locks, atomics, or cross-thread lifetime management.

## Object Access

Rules:

- `loop`, handles, and requests are not generally thread-safe;
- callback slots must be installed and replaced from the loop-owning thread;
- `user_data<T>()` is a raw application pointer and has no synchronization;
- destroying wrappers from another thread while the loop may reference them is a lifetime error;
- native access through `native()` follows the same thread-safety rules as the underlying libuv object.

This keeps the core wrapper zero-overhead and avoids implying guarantees libuv does not provide.

## Cross-Thread Communication

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

## Future High-Level Layer

A future higher-level layer may provide synchronized queues, executor-style APIs, or thread-safe owners. Those facilities should be explicit types built on top of the low-level wrappers, not hidden behavior inside every handle/request.
