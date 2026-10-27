# Raw pipes

Include `<uvpp/handles/pipe.hpp>` or `<uvpp/uv.hpp>`. `uv::raw::pipe` owns inline,
address-stable native storage on the shared `uv::loop`. It is neither copyable nor
movable and has no `uv::pipe` alias. The common `uv::pipe_chmod_flag`,
`uv::pipe_name_flag` and `uv::handle_type` values remain in `uv`.
High-level `pipe_connection` and `pipe_listener` keep their independent storage,
coroutine and `uv::ops` APIs.

## Initialization and lifecycle

```cpp
uv::raw::pipe channel;
auto initialized = channel.init(loop, true); // enable IPC on this endpoint
if (!initialized) return;
```

Default construction does not register a native handle. `init(loop_or_view,
ipc = false) -> status` and the throwing `pipe(loop_or_view, ipc = false)`
constructor share native initialization; constructors call `.throw_if_error()`.
`initialized()` observes the native loop pointer. Repeat init returns `UV_EALREADY`
without changing IPC mode, application `data` or native address, including during
and after close. Storage cannot be reinitialized after successful initialization.
The Unix/Windows native pipe initializers have no fallible step after registration;
no poll-style queue rollback or extra native-lifetime storage is added.

Before init or while closing, operational methods return `UV_EBADF`; descriptor/
buffer queries use the same common result protocol as raw TCP. Default-state
`active`, `closing`, `has_ref` and stream readability/writability are false,
`write_queue_size` is zero, and ref/unref/close are inert. Close is also inert if
already closing. Retain storage through native close completion and drive the loop
normally. There is no nested-loop destructor cleanup.

[Stream subscriptions](raw-stream.md) provide listen, accept, read start/stop,
blocking, runtime/static replacement and all normal close forms. The application
owns native `data`; callbacks reconstruct wrappers without using that field.
All methods follow loop-thread affinity, and callback exceptions terminate at the
native C boundary.

## Names, descriptors and configuration

| Operation | Outcome |
| --- | --- |
| `open(uv_file)` | `status`; native descriptor adoption on success |
| `bind(string_view)` | `status` |
| `sockname()`, `peername()` | `result<string>` with the native name bytes |
| `pending_instances(int)` | `status`; positive counts only |
| `pending_count()` | Integer pending IPC-handle count |
| `pending_type()` | `uv::handle_type` of the next pending IPC handle |
| `chmod(int)` / `chmod(pipe_chmod_flag)` | Native `status` |

Name queries allocate C++ storage, propagate allocation exceptions separately,
and return native failures in the result. They preserve exactly the reported name
length, including embedded/trailing NUL bytes in Linux abstract names. Names of
unbound/accepted endpoints follow native backend conventions and can be empty.
`pending_count` and `pending_type` are native observations while initialized,
including while closing; before init they return zero and `unknown` respectively.
`pending_instances` returns `UV_EINVAL` for nonpositive counts; valid counts retain
libuv's Windows configuration semantics, including ignoring changes after binding.
It is a successful native no-op on Unix. Permission and descriptor validity errors
remain native codes. Failed `open`/bind/configuration does not promise rollback of
arbitrary native side effects.

With `UVPP_HAS_PIPE_BIND2` / `UVPP_HAS_PIPE_CONNECT2` (libuv 1.46+), length-aware
bind/connect overloads accept `unsigned int` flags or `pipe_name_flag`, including
`no_truncate`. On Linux these APIs support an abstract namespace marker (initial
NUL) from libuv 1.46. Additional embedded or trailing NUL bytes require libuv 1.51+
(`UVPP_HAS_PIPE_ABSTRACT_NUL_NAMES`); upstream 1.46–1.50 reject those names with
`UV_EINVAL`. The [libuv 1.51 changelog](https://github.com/libuv/libuv/blob/v1.51.0/ChangeLog)
records this behavior change. The wrapper preserves the native result rather than
requiring a newer libuv or altering the endpoint name.
Native flags/length/path errors remain statuses. Empty names are rejected with
`UV_EINVAL` before submission. On older libuv, the default bind/connect form copies
the name into a temporary terminated string; this preparation can allocate/throw,
and embedded NUL bytes are rejected with `UV_EINVAL` rather than silently changing
the endpoint. Length/truncation limits otherwise follow native behavior. Native
name-buffer APIs remain available through explicit `native()` access if the caller
needs to supply storage itself.

## Connect and write requests

`connect(request, name, callback)` and `connect_static<Callback>(request, name)`
return submission statuses with a separate `uv::raw::connect_request`.
With connect2, flag-taking overloads share that result contract. All paths reject
pending reuse with `UV_EBUSY`, commit callbacks only after native acceptance and
release pending state before completion delivery, permitting reentrant reuse.
Runtime callable preparation can throw before method entry. Legacy native
`uv_pipe_connect` returns void: after validation/preparation, submission succeeds
and native failures arrive at completion. Connect2 can also reject immediately.
No callback is retained or delivered for an immediate rejection.

Inherited write/shutdown and their static forms use the
[raw request protocol](raw-requests.md). Callback/request/payload lifetimes remain
independent of handle subscriptions; close does not retire them before their
actual native completion. Result-oriented methods do not translate C++ allocation
or user-callable exceptions into native error codes.

## IPC handle export

`write_with_handle(request, buffers, exported, callback)` and
`write_with_handle_static<Callback>(request, buffers, exported)` return `status`
and use `uv::raw::write_request`. Both accept a `span<const buffer_view>` or a
single `buffer_view`. They borrow payload and exported native handle through write
completion, allocate no extra C++ operation state, and do not transfer ownership
of the local exported wrapper. Native metadata costs remain libuv's responsibility.

The sending pipe must have IPC enabled. Both wrappers must be initialized and
non-closing; exported native types must be TCP or pipe. Export may borrow a handle
from another loop, subject to both handles' loop-thread affinity; it does not
register that handle on the sending loop. Empty
buffer lists and positive-length null views return `UV_EINVAL`. Pending requests
return `UV_EBUSY` before these validations. Runtime/static modes share rejection,
completion extraction and reentrant reuse. Native export support remains
platform-dependent: Windows supports TCP export, while Unix also supports pipe
handles. Other native restrictions retain their native error codes.

`write_with_handle_now(buffers, exported)` accepts the same buffer forms and returns
`write_now_result`, preserving byte count, partial progress, would-block and errors.
On Windows native immediate handle transfer returns `UV_EAGAIN`; use the request
form for supported transfer rather than interpreting retries as eventual support.
The receiving IPC-enabled endpoint uses `pending_count`, `pending_type` and
`accept` into a separate initialized handle. The ordinary listener itself uses
`ipc = false`; enable IPC on the connected/accepted endpoints that exchange handles.
