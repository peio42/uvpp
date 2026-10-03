# Filesystem notifications

`uv::fs_event_source` is an experimental v3 owner for one persistent native
filesystem subscription. Construction starts watching immediately:

```cpp
#include <uvpp/fs_event_source.hpp>

uv::co::task<void> observe(uv::loop &loop, std::string_view path) {
  uv::fs_event_source source{loop, path};
  auto event = co_await source.next();
  if (event.filename) {
    // An owned filename, independent of libuv callback storage.
  }
  if (event.events.has(uv::fs_event_flag::change)) {
    // Inspect the filesystem again if the application needs its current state.
  }
  co_await source.close();
}
```

`fs_event` contains `std::optional<std::string> filename` and `fs_event_flags events`.
The filename is absent when libuv supplies no name or when coalescing makes it
ambiguous. A supplied empty string remains a present name. For directory watches,
libuv normally supplies a relative path. `fs_event_flag::{rename,change}` names
notification bits; `fs_event_option::{watch_entry,stat,recursive}` names separate
start options, combined in `fs_event_options`:

```cpp
uv::fs_event_source source{loop, path, uv::fs_event_option::recursive};
auto outcome = co_await uv::ops::next(source); // uv::result<uv::fs_event>
co_await uv::ops::close(source);              // uv::status
```

Options are passed to libuv without emulating unsupported behavior. Consult the
[libuv fs-event contract](https://docs.libuv.org/en/v1.x/fs_event.html):
`watch_entry` and `stat` are documented as unimplemented, and recursive support
is platform-dependent. An option's presence in the enum does not establish support
on the current backend.

## Pending notifications

There is one exclusive coroutine waiter. A competing `next()` fails with
`UV_EBUSY`. Successful delivery releases the waiter and its cancellation
registration before resuming the task, so immediate rearming is supported. The
native subscription remains active between waits.

Without a waiter, the source retains one pending outcome:

- Successful notifications merge flags with bitwise OR.
- A name is retained only if every merged notification supplies the same name.
  Missing or different names make the batch anonymous until it is consumed.
- A native callback error replaces any pending success. The first pending error
  remains until consumption; subsequent notifications are discarded meanwhile.
- The next wait consumes the pending outcome once, synchronously at suspension.

This deliberately loses event counts, ordering, and individual names. There is no
unbounded event queue and no promise of lossless filesystem observation.

## Errors, cancellation, and close

`next()` throws native errors at the await; `uv::ops::next(source)` returns them
as `result<fs_event>`. Closed or moved-from owners report `UV_EBADF`.
Loop-affinity violations throw `std::logic_error` in either surface. All owner
operations, moves, and destruction belong on the loop thread.

Cancelling a task detaches only its current waiter and reports `UV_ECANCELED`.
The watcher stays active and another non-stopped task may wait again. A previously
requested stop rejects `next()` before consuming a pending outcome. Cancellation
and callback delivery are serialized on the loop thread; the first delivery wins.
Native callback errors are reported without automatically closing the source;
continued notifications after such an error depend on the backend, with no recovery
promise.

`close()` marks the source terminal, calls `uv_fs_event_stop()`, cancels a current
waiter, clears pending state, and submits `uv_close()`. Once initiated, close is
non-cancellable. Repeated closes join the same native completion, including from
stopped tasks. `request_close()` starts that transition without waiting. Destruction
also starts cleanup and retains native storage until completion; it never drives a
nested loop. Keep the loop alive and drive it through all closes. Resource-scope
adoption is not implemented for this source.

Construction throws native init/start errors and ordinary C++ setup exceptions.
The path is copied into temporary storage before initialization, and uvpp does not
retain that copy after `uv_fs_event_start()` returns. Embedded NULs are rejected
with `UV_EINVAL`. If native start fails after successful initialization, rollback
retains storage through asynchronous close: drive the loop before closing it.
Filename copying in a native callback can allocate; a C++ materialization failure
is captured and rethrown at the next await in **both** surfaces, or retained as the
pending failure until consumption. No exception escapes the native callback.

## Ownership, costs, and portability

The source owns stable `uv_fs_event_t` and subscription/close state, never the
watched file or directory. Moving it preserves native addresses and active waits.
`native()` and `native_handle()` borrow native storage; they do not permit replacing
reserved `data`, replacing the callback, independently starting/stopping the watcher,
or taking over its close/lifetime. Querying native state is supported.

Construction allocates owner storage and prepares a path string. libuv has its own
backend/path storage costs. Each notification may allocate for its owned filename;
delivery moves that value to the awaiter and caller. At most one pending outcome
is retained, plus the current callback's temporary value. Coroutine frames and
joining close waiters may also allocate. No allocation-free claim is made.

Notifications expose libuv's native observations, rather than definitive statements
that a particular file was renamed, removed, or durably changed. Backend, platform,
filesystem, and directory-versus-file watching affect delivery, flags, names,
duplicates, and coalescing. Applications needing a current model should rescan or
query the filesystem and tolerate missing/merged notifications. Broader platform
validation remains open.

The historical callback wrapper is now `uv::raw::fs_event` in
`<uvpp/handles/fs_event.hpp>`. It retains its existing callback and throwing
submission API; its complete raw error-policy migration remains unfinished.
Its callback's `raw::fs_event_result::filename()` borrows native memory only for
the callback duration. The high-level `uv::fs_event` is an owned result value.
