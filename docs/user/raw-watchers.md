# Raw signal, poll, and filesystem watchers

`uv::raw::signal`, `uv::raw::poll`, and `uv::raw::fs_event` provide caller-owned
callback subscriptions on the shared `uv::loop`. Include their corresponding
`<uvpp/handles/signal.hpp>`, `<uvpp/handles/poll.hpp>`, or
`<uvpp/handles/fs_event.hpp>` header, or `<uvpp/uv.hpp>`.

These wrappers are neither copyable nor movable. Keep their storage alive through
native close completion, explicitly call `close()`, and drive the loop through
that completion. Destructors do not close or run the loop. Native `data` is
application-owned; native access does not permit independently replacing wrapper
callbacks or bypassing its subscription/close protocol.

Use the high-level [signal source](signals.md), [poll source](poll.md), or
[filesystem event source](fs-events.md) when you want movable ownership and
coroutine waits. Those owners retain their existing throwing and `uv::ops`
contracts. Their native objects are not raw wrapper instances.

## Errors and subscription state

All `start`, `start_static`, signal `start_oneshot` variants, and `stop` operations
return `uv::status`. Inspect the initiating status independently of later event
results. A rejected start does not install or retain its new runtime callable.
The old callable remains installed only while the native source remains active;
a native reconfiguration failure can itself stop that source. No portable
transactional rollback of native subscription state is promised. Starts and stops
on closing handles return `UV_EBADF`.

Successful stop releases the stored runtime callable. Successful replacement,
including switching to a static callback, releases the old callable. An executing
callback remains alive until it returns. Ordinary notifications retain a
persistent subscription; reentrant stop/replacement is respected when that
callback returns. Supply a valid callback and follow loop-thread affinity.

A negative poll/fs-event callback status is terminal for that raw subscription:
the wrapper stops the native source and releases its slot before delivering the
error. The callback may immediately start a replacement. An unexpected failure
to stop during terminal delivery terminates rather than releasing live callback
storage. The high-level fs-event source has its own persistent error-retention
policy, documented separately.

Close quiesces native delivery and releases the subscription callable before
returning; its close callback runs later. Retain wrapper storage until then.
Exceptions escaping runtime or static user callbacks terminate at the C boundary.

Construction remains throwing initialization: native init failures raise
`uv::error`. Preparing a runtime `std::function` or a filesystem path may allocate
and throw C++ exceptions before native start. There is no hidden uvpp operation
allocation, lock, queue, or coroutine state. libuv may allocate its own native
subscription storage; no measured allocation-free guarantee is made.

The shared native queries are explicit-result operations on these wrappers too:
`fileno()` returns `result<uv_os_fd_t>`, buffer-size getters return `result<int>`,
and buffer-size setters return `status`. Native errors, including unsupported
queries on a given handle type, remain results. Common loop/value and borrowed
`handle_view` policies are independent of this watcher migration.

## Signal

```cpp
uv::loop loop;
uv::raw::signal interrupt{loop};
auto started = interrupt.start_oneshot(SIGINT,
  [](uv::raw::signal& self, uv::signal_number signum) {
    // The subscription is already stopped here.
    self.close();
  });
if (!started) {
  interrupt.close();
}
loop.run();
loop.close();
```

`signal_number` remains in `uv`. Persistent `start` and
`start_static<Callback>` deliver `(raw::signal&, signal_number)` repeatedly.
`start_oneshot` and `start_oneshot_static<Callback>` stop before their sole delivery,
allowing runtime/static rearming inside the callback. This uses a persistent
native signal source with wrapper-controlled stop at first delivery, rather than
`uv_signal_start_oneshot`: it does not promise native signal-handler one-shot
semantics before the loop dispatches the notification. Signal delivery can
coalesce, and signal disposition remains subject to libuv/platform restrictions.

## Descriptor readiness

`uv::raw::poll` borrows its descriptor or socket. The common `poll_event`,
`poll_events`, `socket_poll`, and mask operators remain in `uv`:

```cpp
uv::raw::poll readiness{loop, fd};
auto started = readiness.start(uv::poll_event::readable,
  [](uv::raw::poll& self, uv::raw::poll_result result) {
    if (!result) {
      // result.error_code(); the subscription has stopped.
      self.close();
      return;
    }
    // Consume readiness without blocking. Stop/close when finished.
  });
```

Readiness is a persistent raw subscription, unlike the per-wait high-level
`poll_source`. Poll initialization can set the external descriptor non-blocking;
close does not close that descriptor. Keep it open through native poll close.
Use `socket_poll` as the third constructor argument for `uv_poll_init_socket`.

`start_raw(int, cb)` and `start_static_raw<Callback>(int)` select an integer mask.
Unknown bits report `UV_EINVAL` before native submission; the existing
`UVPP_HAS_POLL_DISCONNECT` and `UVPP_HAS_POLL_PRIORITIZED` gates apply. A zero mask
stops polling and releases the runtime callback. Both dispatch forms return a
`raw::poll_result`, retaining `events()`, `raw_events()`, and `has_event()`.

## Filesystem events

`uv::raw::fs_event` starts on a path with optional `raw::fs_event_flag` flags.
Runtime and static overloads share the same contract. Paths containing embedded
NUL report `UV_EINVAL` before native start. Path preparation precedes native
submission and callback installation.

`path()` returns `result<std::string>`: its success value owns a copy, while native
query failures are explicit errors. This query allocates a temporary buffer and
may resize it before materializing its string; allocation exceptions still throw.

`raw::fs_event_result::filename()` borrows native memory only during callback
delivery; copy it before retaining it. Missing filenames produce an empty view.
Flags use `raw::fs_event_kind` and `has()`. Both raw event result families expose
`ok()`, boolean conversion, `status()`, `raw_status()`, and `error_code()`;
successful statuses normalize to zero without losing their event payloads.

Filesystem notifications retain libuv's backend-dependent names, flags,
coalescing, and loss limitations. See [libuv's fs-event documentation](https://docs.libuv.org/en/v1.x/fs_event.html).
`fs_poll` remains outside this migration slice.
