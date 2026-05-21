# Future Features

This page tracks features identified for uvpp `2.x` after the `2.0.0` API
stabilization. These additions are intended to extend the library without
changing the low-level contracts already published in `2.0.0`.

## Network Utilities

- `getaddrinfo` and `getnameinfo` wrappers with typed request/result objects.

## Work Queue And Random

- `queue_work` wrappers for libuv thread-pool work.
- `random` wrappers for asynchronous and synchronous random byte generation.

## Coroutines

- Coroutine adapters for selected asynchronous operations.
- Coroutine support should layer on top of the explicit request/lifetime model
  rather than replacing the low-level API.

## UDP Batch Builder

`2.0.0` exposes `udp_send_many_view` and `udp::send_many_now()` as the low-level
shape for `uv_udp_try_send2()`. The low-level API stays allocation-free by
requiring caller-provided libuv batch arrays. A future `2.x` release may add a
fluent builder that owns the batch metadata while still borrowing payload
buffers.

Possible shape:

```cpp
auto batch = uv::udp_send_batch{}
  .add(first_buffers, destination)
  .add(second_buffers, destination);

auto result = socket.send_many_now(batch);
```

The builder should own only the metadata needed to call libuv. It must not imply
that payload bytes are copied or that asynchronous state is hidden.
