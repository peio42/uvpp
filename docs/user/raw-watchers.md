# Raw callback watchers

`uv::raw::{signal,poll,fs_event,timer,idle,prepare,check}` provide caller-owned
callbacks on the shared `uv::loop`. Include the corresponding
`<uvpp/handles/<name>.hpp>` header or `<uvpp/uv.hpp>`.

These wrappers are neither copyable nor movable. Keep their storage alive through
native close completion, explicitly call `close()`, and drive the loop through
that completion. Destructors do not close or run the loop. Native `data` is
application-owned; native access does not permit independently replacing wrapper
callbacks or bypassing its subscription/close protocol.

For cross-thread notification, see [`uv::raw::async`](async.md), which is active
from initialization and has no start/stop protocol. Its send/close synchronization
is caller-controlled.

Use the high-level [signal source](signals.md), [poll source](poll.md), or
[filesystem event source](fs-events.md) when you want movable ownership and
coroutine waits. Those owners retain their existing throwing and `uv::ops`
contracts. Their native objects are not raw wrapper instances.

## Errors and subscription state

All `start`, `start_static`, signal `start_oneshot` variants, and `stop` operations
return `uv::status`. Timer `again` and `set_repeat` also return `uv::status`. Inspect the initiating status independently of later event
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


## Timers

`uv::raw::timer` supports `start(timeout, cb)`, `start(timeout, repeat, cb)`,
and `start_static<Callback>(timeout, repeat = 0ms)`. Callbacks receive `timer&`.

```cpp
using namespace std::chrono_literals;
uv::loop loop;
uv::raw::timer timer{loop};
auto started = timer.start(10ms, [](uv::raw::timer& self) {
  // This one-shot subscription has already finished. A fresh start is allowed.
  self.close();
});
if (!started) timer.close();
loop.run();
loop.close();
```

A repeating timer retains its callback between firings. Libuv rearms it before
user delivery; the wrapper uses actual native activity at delivery to distinguish
persistent events from terminal one-shot completion. Terminal delivery extracts
the runtime callable before invoking it, so captures are released when that
invocation returns even if the timer remains open. A callback can immediately
start a replacement, with either dispatch form. There is no extra operation state
or per-firing uvpp allocation.

`stop()` and `close()` release the subscription callable. `again()` only reuses an
active subscription, returning `UV_EINVAL` if the timer is inactive (including
never started, explicitly stopped, or completed one-shot). Start a new
subscription with a callback after those terminal states. This deliberately
narrows native `uv_timer_again`, which can reuse a stopped timer's native callback;
raw uvpp does not resurrect a released callback. For an active subscription,
`again()` follows native repeat timing; a zero repeat makes it a successful no-op.
All mutating methods return `UV_EBADF` while closing.

`set_repeat(interval)` changes the configured repeat without changing an already
scheduled timeout. Setting repeat to zero from a repeating callback still permits
the already-scheduled next firing; that final delivery releases the callback.
Setting a positive repeat on an inactive timer does not restart it. See
[libuv's timer timing rules](https://docs.libuv.org/en/v1.x/timer.html).

Duration inputs accept standard integral and floating-point chrono representations.
Finite negative values first normalize to zero, then conversion to whole
milliseconds truncates toward zero, without rounding up. For example, `-2ms`
and `500us` both become `0ms`, while `1500us` becomes `1ms`. A zero timeout
requests delivery on a subsequent loop iteration, not inline: an elapsed duration
can therefore request an immediate timer without imposing a minimum one-millisecond
delay. A repeat interval converted to zero disables repetition. This differs
from `uv::co::sleep_for`, which rounds positive fractional milliseconds up.
Non-finite or overflowing values report `UV_EINVAL` before native
mutation or callback installation. Integral native-millisecond counts retain the
full `uint64_t` range; ordinary integral units with an integer conversion factor
are checked without overflowing intermediate chrono arithmetic. Other ratios and
floating-point values convert through `long double`, rejecting values at or above
2^64 milliseconds. This conversion can inherit floating-point rounding near
integral boundaries; use integral milliseconds for exact native counts.

`repeat()` and version-gated `due_in()` return `timer::duration`, a
`std::chrono::duration<uint64_t, std::milli>`, preserving native unsigned counts
rather than narrowing them into signed milliseconds. `due_in()` retains the
`UVPP_HAS_TIMER_GET_DUE_IN` capability gate (libuv 1.40.0). These native getters
have no operational failure channel and do not check `closing()`. They observe
native state while the initialized handle's storage remains valid, including
during closing. The associated loop must also remain alive for these queries.
These observations do not imply that the timer remains active or can be restarted;
the mutating methods still reject closing handles with `UV_EBADF`.
Construction remains throwing initialization;
callback preparation may allocate before `start` runs. Raw timeout rounding and
lifetime rules do not change the separate high-level `uv::co::sleep_for` contract.

## Idle, prepare, and check

`uv::raw::{idle,prepare,check}` expose `start(cb)`, `start_static<Callback>()`, and
`stop()`. Their callbacks receive a reference to their wrapper and carry no native
completion error. Start/stop errors use the immediate `status` result.

All three retain their subscription between loop iterations until stop or close.
Successful replacement supports runtime-to-runtime, runtime-to-static,
static-to-runtime, and static-to-static transitions, including from inside the
executing callback. Each start stops and restarts native delivery before committing
the new callable because native start on an already-active phase watcher does
not replace its callback. This can change that handle's order relative to other
watchers of the same phase; no order among peers is promised. Stop and close
release stored captures, while an executing callable survives until it returns.

Idle callbacks run each iteration while active and force a zero poll timeout;
they are not a low-priority background-work facility. Prepare callbacks run
before polling, and check callbacks after polling. An active prepare/check
watcher alone does not guarantee that polling returns promptly; use an actual
wake source or `run(run_mode::nowait)` when appropriate. There is no new high-level
phase owner, coroutine frontend, queue, or cross-thread entry point in this slice.
