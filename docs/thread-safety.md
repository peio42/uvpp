# Thread Safety

## Baseline

uvpp v2 follows libuv's threading model. Unless a function is explicitly documented as thread-safe by libuv and by uvpp, wrapper objects must be accessed from the thread that owns the associated `uv_loop_t`.

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

Cross-thread work should use libuv mechanisms designed for it, such as `uv_async_t`, once uvpp exposes the corresponding wrapper.

The intended shape is:

```cpp
uvpp::async wakeup(loop, callback);

// other thread
wakeup.send();
```

Only the specific cross-thread entry points documented by libuv should be callable from other threads. The callback still runs on the loop thread.

## Future High-Level Layer

A future higher-level layer may provide synchronized queues, executor-style APIs, or thread-safe owners. Those facilities should be explicit types built on top of the low-level wrappers, not hidden behavior inside every handle/request.
