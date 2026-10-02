# Descriptor readiness

`uv::poll_source` is an experimental v3 owner for readiness waits on an external
file descriptor or socket. It owns stable `uv_poll_t` storage and **borrows** the
descriptor: neither close nor destruction closes that descriptor. Libuv
initialization sets it to non-blocking mode.

```cpp
#include <uvpp/poll_source.hpp>

uv::co::task<void> observe(uv::loop& loop, int fd) {
  uv::poll_source source{loop, fd};
  auto events = co_await source.next(uv::poll_event::readable);
  if (events.has(uv::poll_event::readable)) {
    // Attempt non-blocking I/O on fd; EAGAIN remains possible.
  }
  co_await source.close();
  // The caller still owns fd and may close it.
}
```

Construction initializes an inactive handle. `next(mask)` arms native polling
when awaited; a callback stops polling and releases the waiter slot before
resuming the coroutine. The mask can combine `poll_event` values with `|`.
Success returns `uv::poll_events`; another wait may immediately use a different
mask. There is one active waiter: competing waits fail with `UV_EBUSY` without
changing the first wait. Empty masks and unknown bits fail with `UV_EINVAL`.

The source does not watch or retain events between waits. Libuv uses level
triggering: readiness that remains present is detected when polling is rearmed.
Readiness is an indication to attempt I/O, not a payload queue or a guarantee that
I/O will succeed. A successful wait does not read or write the descriptor.
`poll_event::disconnect` is available when `UVPP_HAS_POLL_DISCONNECT` is true
(libuv 1.9.0 or newer); `poll_event::prioritized` requires
`UVPP_HAS_POLL_PRIORITIZED` (libuv 1.14.0 or newer). Unsupported events are absent
from the public enum and rejected by `next()` if passed through `from_raw()`.
`disconnect` is an optional notification, not a portable substitute for observing
EOF through the actual I/O operation.

## Errors, cancellation, and close

`next(mask)` throws native submission or completion errors at the await.
`uv::ops::next(source, mask)` reports those same errors in
`uv::result<uv::poll_events>`:

```cpp
auto result = co_await uv::ops::next(source, uv::poll_event::readable);
if (result) {
  auto events = result.value();
  // Inspect events and attempt I/O.
} else {
  // Inspect result.error().
}
```

Task cancellation stops its native wait, releases the waiter slot, and delivers
`UV_ECANCELED`. The source stays open and can be reused by a task whose cancellation
context has not been stopped. A native completion error likewise stops that wait;
further use depends on the descriptor remaining valid.

`close()` is terminal and non-cancellable once initiated. It marks the source
closing, stops native polling, cancels an active waiter with `UV_ECANCELED`, and
waits for the native close callback. Repeated close awaits join that completion;
`uv::ops::close(source)` supplies the explicit-result counterpart.
`request_close()` starts cleanup without awaiting it. Destruction starts the same
cleanup and retains native storage through completion without running a nested
loop. The borrowed loop must survive and be driven through native close.
Waits on closing, closed, or moved-from sources fail with `UV_EBADF`.

The owner is move-only; moving it preserves the native address and an active wait.
Native `data` is reserved for stable internal state. `native()` and
`native_handle()` provide explicit borrowed access; they do not authorize replacing
`data`, callbacks, or independently starting, stopping, or closing the handle.
All source operations follow loop-thread affinity. Awaiting from another execution
loop raises `std::logic_error`, including through `uv::ops`.

Construction allocates stable owner storage and may throw allocation or native
initialization errors. Each wait stores its bookkeeping in the awaiter; coroutine
frames and close-waiter registration may allocate. Allocation/performance
measurements remain deferred. `resource_scope` adoption and a persistent
subscription interface are outside this slice.

## Descriptors and platforms

For sockets use the explicit tag, preserving the platform's native socket type:

```cpp
uv::poll_source source{loop, socket, uv::socket_poll};
auto events = co_await source.next(uv::poll_event::readable |
                                  uv::poll_event::writable);
```

Windows supports sockets only; the tagged overload uses `uv_poll_init_socket()`.
Unix also supports pollable descriptors, such as pipes; support for arbitrary
file types depends on the native backend. No Windows validation is claimed by the
current Linux tests.

Do not close or replace the descriptor while polling is active, or install multiple
active poll handles on the same socket. Close the source before releasing the
borrowed descriptor. Poll sources are intended for integration with external I/O;
use the TCP/UDP/pipe owners for their supported transports.

These platform and readiness rules follow the
[libuv poll documentation](https://docs.libuv.org/en/v1.x/poll.html).
