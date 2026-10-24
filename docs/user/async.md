# Raw async notifications

`uv::raw::async` is a caller-owned notification handle on the shared `uv::loop`.
Include `<uvpp/handles/async.hpp>` or `<uvpp/uv.hpp>`. It is neither copyable nor
movable, its native address stays stable, and native `data` belongs to the
application. There is no `uv::async` compatibility alias.

Unlike timer and phase watchers, native async initialization activates the handle
immediately. The wrapper has no `start()` or `stop()`. Sending schedules a
notification on the loop thread; it never calls the user callback inline on the
producer thread. Multiple sends can coalesce into one callback, so notifications
are not message counts. See [libuv async handles](https://docs.libuv.org/en/v1.x/async.html).

## Construction and errors

Construct from `loop&` or `loop_view`, with an optional runtime callback or a
static callback tag:

```cpp
void on_wakeup(uv::raw::async& self);

uv::raw::async runtime{loop, callback};
uv::raw::async static_dispatch{
  loop, uv::raw::async::static_callback<on_wakeup>{}
};
```

Both callback forms receive `uv::raw::async&`. Construction without a callback
is allowed: notifications are consumed without user delivery until one is
installed. The handle still remains active and referenced by default. Native
initialization errors throw `uv::error`. Runtime callable preparation may allocate
or throw before initialization; committing the prepared callable after native
init uses a non-throwing move.

`send() noexcept` returns `uv::status` from the native submission result. Success
means notification submission, not user delivery, task acceptance, or one callback
per send. There is no later native completion-error argument in the callback.

`set_callback(cb)` and `set_callback_static<Callback>()` also return `uv::status`.
They require the loop thread and return `UV_EBADF` while closing. Preparing a
`std::function` argument may still throw before `set_callback` is entered; failure
leaves the current callback intact, including during reentrant replacement.
Static callback templates use `std::invocable` constraints.

The shared `fileno()` and buffer-size queries return results, and buffer-size
setters return status, including native errors for unsupported handle queries.
These queries, `ref()`/`unref()`, native access, and all configuration remain
loop-thread operations. A borrowed `.view()` does not transfer close authority or
make other handle operations cross-thread safe.

## Callback replacement

One native trampoline is installed at construction. Each delivery selects either
the runtime callable or a stored static function pointer. Runtime/static changes
never reinitialize the handle or independently replace its native callback.

Replacement affects pending notifications according to the callback installed
when the loop delivers them. It releases old stored captures; an executing
callable remains alive until that invocation returns. Reentrant replacement in
either direction is supported, and an old runtime invocation cannot restore its
callable over the replacement. Static dispatch snapshots its function pointer
before calling user code.

`set_callback({})` switches to an empty runtime slot, releasing stored captures
and dropping notifications while no callable is installed. It does not stop or
unreference the native handle. `unref()` changes loop liveness, not notification
or callback ownership; an unreferenced handle alone does not keep the loop running.
Exceptions escaping runtime or static callbacks terminate at the native C boundary.

## Threads, data, and close

Only `send()` is a cross-thread entry point. Callback installation, replacement,
close, and destruction happen on the loop thread. Producers may send while that
thread delivers or replaces callbacks, provided the handle remains initialized
and open throughout every send.

Before `close()`, prevent new producer sends and wait for every in-flight send to
return, for example by joining producer threads or using an application-owned
synchronization protocol. This ordering is the caller's responsibility. Keeping
the wrapper's storage alive alone does not make a concurrent close/send valid.
The wrapper adds no lock, atomic closing flag, reference counting, or producer
endpoint. Use high-level `loop_posting` when you need its synchronized queue and
lifetime-safe producer endpoints.

`send()` deliberately does not read `closing()`: that loop-thread flag is not a
cross-thread lifetime guard. Sending during/after close violates the raw protocol;
there is no wrapper guarantee that such misuse reports `UV_EBADF`. The setter
methods' closing checks do not extend to cross-thread send admission.

The following example joins its sole producer before any close can run:

```cpp
#include <thread>
#include <uvpp/handles/async.hpp>

uv::loop loop;
uv::raw::async wakeup{loop, [](uv::raw::async& self) {
  self.close();
}};
uv::status submitted;
std::jthread producer{[&] { submitted = wakeup.send(); }};
producer.join();
if (!submitted) wakeup.close();
loop.run();
loop.close();
```

For a shared work queue, protect that queue with a mutex or another explicit
publication protocol, enqueue before sending, and drain the queue on notification.
The wrapper owns no queue or payload. Native memory-order guarantees depend on
the linked libuv version; its documented send/callback sequential consistency
was strengthened in 1.53.0. This wrapper neither adds fences nor promises that
stronger guarantee on older versions. Application synchronization must also cover
conflicting accesses while producers continue to write, independent of wakeup
coalescing. See [libuv's version-specific memory-order notes](https://docs.libuv.org/en/v1.x/async.html).

After producers are quiescent, `close()`, `close(cb)`, or `close_static<Callback>()`
quiesces native delivery and clears runtime/static callback selection. Stored
captures are released; an executing callback remains alive until return. Pending
notifications are not a queue that close promises to drain. Keep wrapper storage
and the loop alive through native close completion. A close callback may release
the wrapper; its destructor does not close implicitly or run the loop.

## Costs and boundaries

The wrapper uses the existing raw watcher callback/close/query machinery and one
additional static dispatch function pointer per handle. Runtime `std::function`
construction can allocate; callback replacement moves prepared storage. There is
no additional uvpp operation allocation, queue, mutex, coroutine frame, or hidden
ownership retention. libuv backend storage costs remain platform/version dependent;
no measured allocation-free guarantee is claimed.

`uv::loop_posting` retains its own native `uv_async_t`, reserved `data`, synchronized
queue, producer lifetime, and drain-on-close protocol. It is independent of the
raw wrapper and is not migrated onto caller-owned raw storage by this change.
