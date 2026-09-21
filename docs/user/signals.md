# Signals

`uv::signal_source` is an experimental v3 owner for one persistent libuv signal
subscription. Constructing it starts listening immediately; its signal number
does not change during its lifetime.

```cpp
#include <csignal>

#include <uvpp/signal_source.hpp>

uv::co::task<void> wait_for_interrupt(uv::loop& loop) {
  uv::signal_source interrupt{loop, SIGINT};

  for (;;) {
    auto signum = co_await interrupt.next();
    // signum is SIGINT here. Decide whether to continue or close the source.
    co_await interrupt.close();
    co_return;
  }
}
```

`next()` waits for one event, but does not call `uv_signal_start()` or
`uv_signal_stop()`. The source stays subscribed after successful delivery, so a
loop can await it repeatedly. There may be one active `next()` at a time; another
one completes with `UV_EBUSY`.

When no `next()` is active, the source keeps one pending notification. Several
arrivals in that interval are coalesced; the next `next()` completes immediately
with the configured signal number. This is a notification source, not a counted
signal queue.

## Cancellation and close

Stopping the awaiting task cancels only its current wait. `next()` throws
`uv::error{UV_ECANCELED}` and `uv::ops::next(source)` returns
`result<signal_number>{UV_ECANCELED}`; the source remains subscribed and another
non-stopped task may use it.

```cpp
auto event = co_await uv::ops::next(source);
if (!event && event.error() == uv::make_error_code(UV_ECANCELED)) {
  // The source remains usable by a task without a pending stop request.
}
```

The current task's cooperative stop request remains set, so immediately calling
`next()` again from that same task returns `UV_ECANCELED`. Start or resume work
under a cancellation context that has not been stopped to wait again.

`close()` is terminal. It calls `uv_signal_stop()`, cancels a current wait with
`UV_ECANCELED`, then waits for the native close callback. `request_close()` starts
the same transition without waiting. A closed source cannot be restarted. Keep the
loop alive and drive it through close completion; destruction starts cleanup but
does not run a nested loop. `resource_scope` does not yet adopt signal sources.

The owner is move-only and moving it preserves its native address. `native()` and
`native_handle()` provide explicit borrowed libuv access; they do not transfer
close authority. As with every high-level owner, the native `data` member remains
for application use.

The source allocates stable owner storage at construction. `next()` adds no
library allocation; coroutine frames and close waiters may still allocate.

## Platform behavior

Signal delivery follows libuv's platform rules. In particular, Windows emulates
only selected console signals, and `raise()` does not trigger a libuv signal
watcher there. Do not use a source for signals that libuv documents as impossible
or unsafe to catch on the target platform.
