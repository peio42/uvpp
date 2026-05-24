# Future Features

This page tracks features identified for future uvpp v2 releases. These
additions are intended to extend the library without changing the published
low-level v2 contracts.

## Coroutines

- Coroutine adapters for selected asynchronous operations.
- Coroutine support should layer on top of the explicit request/lifetime model
  rather than replacing the low-level API.

## Thread and Synchronization Primitives

libuv provides cross-platform wrappers around OS threading and synchronization
primitives. In a C++20 codebase `std::thread`, `std::mutex`, and related
standard facilities cover most use cases, so these wrappers have lower
priority. A future v2 release may expose them for callers that must stay within
the libuv ecosystem, and as building blocks for explicit higher-level
cross-thread facilities.

Planned scope: `uv_thread_t`, `uv_mutex_t`, `uv_rwlock_t`, `uv_sem_t`,
`uv_cond_t`, `uv_barrier_t`, `uv_key_t`, and `uv_once`.

Design direction is tracked in
[Threading primitives strategy](design/threading-primitives.md). These
wrappers are not a prerequisite for coroutine awaitables; coroutine support
should primarily resume from libuv completion callbacks on the loop thread.
