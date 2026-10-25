# Error Handling

This document describes the implemented common contracts in
[core/error.hpp](../../include/uvpp/core/error.hpp) and the remaining specialized
domain results. Broader `uv::ops` adaptation is tracked in
[proposal 006](../proposals/006-errors-and-results.md).

## V3 operation surfaces

Implemented high-level network awaits throw native operational failures at the
await expression, including submission failures. `uv::ops::close` reports the same
owner-close failures as `uv::status`. DNS resolution is the first paired request
operation: `uv::resolve(...)` throws at the await and
`uv::ops::resolve(...)` returns `uv::result<uv::resolved_addresses>` for the same
submission/completion path. TCP/pipe read/write/accept and UDP receive/send now
also pair member awaits with `uv::ops::{read_some,write,accept,recv_from,send_to}`.
Read/receive wrap the existing domain outcome in `result<T>`; accept returns a
move-only connection result, and write/send return `status`. Borrowed views share
the connection/socket forms. An internal result adapter constructs the original
non-movable awaiter in place and delegates readiness and suspension; terminal
result delivery is the only policy difference. Throwing members unwrap that same
result. Native state, callbacks, slot release, cancellation, payload borrowing,
and provisional-child close remain unchanged, with no extra operation allocation.
Length validation remains throwing C++ preparation. Connect and IPC adaptation
remain proposed.
Synchronous TCP/pipe listener and UDP socket construction also has a paired
`uv::ops::make_* -> result<Owner>` surface. Private result-oriented state creation
is shared with the throwing constructors, which unwrap with `value()`. Private
state adoption transfers successful stable storage without reinitialization.
Native init failure destroys uninitialized state immediately; bind/listen failure
releases initialized state into asynchronous close before error delivery. Neither
surface drives a nested loop. C++ preparation precedes native init (including the
legacy pipe-name copy), and allocation failures propagate in both policies.
Raw filesystem and signal/poll/fs-event operations implement explicit operational
results; other low-level
submission helpers below have not yet completed that migration. C++ setup failures
need separate contracts and result-oriented does not imply `noexcept`.

Task promises capture exceptions; child awaits and completed spawn-handle result
observation rethrow them. Current wrong-loop owner awaits raise `std::logic_error`.
Active spawn-handle destruction requests stop; a subsequent root failure without a
public observer routes exactly once through the loop failure policy, which defaults
to termination. These are implementation limits, not a claim that all proposal
error/retention rules are implemented.

## Existing low-level failures and exceptions

Historical low-level submission APIs outside migrated filesystem/watchers throw
`uv::error` on
a negative native submission result.
`uv::error` derives from `std::system_error`; the libuv category formats messages
with `uv_strerror`, while the exception constructor adds the `uv_err_name` context.
The category's name is `libuv`. `uv::error_code` is the public operational-error
value: `native()` returns zero or a negative libuv status, and explicit conversion
produces the equivalent `std::error_code`. Nonnegative values map to an empty
`uv::error_code`.

`throw_if_error(T)` accepts a `std::signed_integral` type, returns the same type
and unchanged value on success, and throws on a negative value. Positive byte
counts are never narrowed to `int`; only negative native error codes are converted
for `uv::error`. Callers must supply libuv error codes for negative values, which
fit `int`. A `uv_errno_t` overload delegates through `int`, allowing
`throw_if_error(UV_EINVAL)` and returning `int` for enum arguments. Other enum
types remain outside this overload. Its return value is used, for example, to
preserve `uv_run()`'s nonzero result. Submission setup can also throw standard exceptions from string,
vector, or callback storage allocation and explicit argument validation.

Non-throwing variants exist selectively: `loop.try_close()` and supported request
`try_cancel()` methods return `uv::error_code`. There is no `tcp.try_bind()` in the current code. Synchronization attempts such as `mutex.try_lock()` use standard attempt
semantics and can throw on unexpected native errors; see
[API policy](api-policy-decisions.md).

## Asynchronous Completion

A native submission failure is reported by the initiating call; a later failure
is reported by a completion callback. These remain distinct channels in the low-level callback implementation.

```cpp
tcp.connect(req, addr, [](uv::raw::connect_request&, uv::status status) {
  if (!status) {
    auto ec = status.error();
    (void)ec;
  }
});
```

`uv::result<T>` stores either one `T` or one `uv::error_code`; `uv::result<void>`
stores success or an operational error. `uv::status` is its alias when the
declaration names an error-only completion. Both expose `has_value()`, explicit
`operator bool()`, `throw_if_error()`, `value()`, and `error()`. `value()` throws `uv::error` on an
error result; `error()` is empty for success. `result<T>` has the usual lvalue and
rvalue accessors and supports move-only `T` without an allocation.
`void throw_if_error() const` checks the same operational error without extracting
or consuming the payload, including on const objects and temporaries. Both void
`value()` and all four typed `value()` overloads delegate their error check to it.
Throwing raw constructors and error-only facades use this explicit check; payload
extraction keeps `value()`. This member does not change the value-returning free
`uv::throw_if_error(native_status)` helper or result storage/layout.

`uv::status` is the semantic alias for `uv::result<void>`. Public APIs should
prefer `status` when the operation carries no success payload; `result<void>`
remains the generic underlying type.

`result<void>` accepts an `error_code`. API implementations adapt a native libuv
status through `uv::status::from_native(status)`, which documents the origin of
the integer.

## Result Families

This table inventories existing low-level result families, not the high-level
coroutine read result: the latter returns a count/EOF outcome and throws operational
failures. UDP owner receive returns size, truncation, and a copied peer address.


The current result API is not uniform across all families:

| Family | Status access | Error access and payload |
| --- | --- | --- |
| `uv::status` (`uv::result<void>`) | `has_value()` / boolean conversion | `error()` |
| DNS and random results | `status()` returns `uv::status`; `raw_status()` returns `int` | Direct `error_code()` plus family payload |
| Public and raw filesystem results | `status()` returns `uv::status`; `raw_status()` returns zero on success or a negative error | Direct `error_code()`; `raw()` retains native payload/status value |
| Stream/UDP read results | `status()` returns `uv::status`; `raw_status()` normalizes successful counts to zero; `count()` retains native byte count/status | Direct `error_code()`; borrowed payload and family flags remain available |
| `raw::fs_event_result`, `raw::poll_result` | `status()` returns `uv::status`; `raw_status()` normalizes success to zero | Direct `error_code()`; typed event payloads remain available |
| `fs_poll_result` | `status()` returns `uv::status` | `status().error()`; no direct `error_code()` or `raw_status()` |

The specialized low-level families expose `ok()` and explicit boolean conversion;
the common result uses `has_value()` instead of `ok()`. Use the payload
accessor to obtain file descriptors or counts; filesystem `raw_status()` normalizes
successful payloads to zero. Do not replace it with an unchecked narrowing of a
successful native byte count. Process exit uses a separate `process_exit` struct
with `status` and `signal` fields, not the libuv submission-error grammar.

An owned value is not necessarily move-only: `getnameinfo_result` owns copyable
strings, and `owned_buffer` owns a copyable vector. `getaddrinfo_result` uniquely
owns a native `addrinfo` list and is move-only. Raw `opendir_result` and `directory`
require explicit ownership consumption/close rather than automatic resource close.

## EOF and Immediate I/O

Stream `read_result::eof()` recognizes `UV_EOF`, but `ok()` and boolean conversion
are false for this negative value. Test EOF separately before treating a false
result as an I/O failure. `status().error()` still maps `UV_EOF` to a code;
the low-level callback does not throw it automatically. Filesystem read EOF is a successful zero-byte
read, with a different native result shape.

Immediate `write_now_result` and UDP send result types have their own contracts:
`would_block()` separates `UV_EAGAIN` from `has_error()`. `error_code()` is empty
for the would-block case, so inspect `would_block()` explicitly. Counts and raw
values use the accessors on the specific type. These operations do not submit an
asynchronous request or call a completion callback.

## Callback Exception Boundary

`detail::invoke_callback` and `invoke_static_callback` catch user exceptions and
call `std::terminate()`. Native trampolines are `noexcept`; result construction
that throws before reaching an invocation helper also terminates. There is no
current loop-level exception handler or deferred error rethrow.

Public filesystem operations normally materialize their owned/scalar result,
clean the native request, and delete operation state before delivering the result
to user code. This does not promise recovery from allocation failure during result
materialization. The same exception boundary applies to loop walking; vector growth
in `loop.handles()` runs inside that boundary.

Tests in [test-core.cpp](../../tests/test-core.cpp),
[test-network.cpp](../../tests/test-network.cpp), and
[test-threadpool-random.cpp](../../tests/test-threadpool-random.cpp) cover core
status mapping, callback termination, one-shot slots, and selected failure paths.
They are not an exhaustive failure-injection suite for every family.

`poll_source::next(mask)` returns `poll_events` and throws operational errors at
the await; `uv::ops::next(source, mask)` returns `result<poll_events>`. Both adapt
native start and callback failures. Invalid masks report `UV_EINVAL`, competing
waits `UV_EBUSY`, completed cancellation `UV_ECANCELED`, and waits on terminal or
moved-from sources `UV_EBADF`. Construction remains throwing setup; execution-loop
mismatch remains programmer misuse (`std::logic_error`). The raw
`raw::poll_result` is not the owner's result shape.

Filesystem coroutine read/write preparation uses the shared checked native-buffer
builder. Its named field assignments support both Unix and Windows `uv_buf_t`
layouts. Unrepresentable buffer lengths throw `std::length_error` in both policies
before submission; these preparation functions are not `noexcept`. Completion
counts stay `ssize_t` until converted to the public byte count; only negative
native errors narrow to `int`. IPC handle receive follows that same error-only
narrowing rule.

The existing pipe wrapper's `write_with_handle_now()` preserves
`uv_try_write2()`'s platform result: Windows does not support immediate handle
transfer and returns `UV_EAGAIN`, even for an unconnected pipe. Its
`write_now_result` therefore has `would_block() == true`, `has_error() == false`,
and an empty `error_code()`. Unix unconnected pipes report a terminal native
error instead. The platform test asserts each outcome separately rather than
accepting either status on every platform.

## Raw filesystem submission

`uv::raw::fs` operations return immediate `uv::status` for both runtime and static
callbacks. A failed submission clears the runtime callable and delivers no
completion; the caller must still clean the native request. Accepted operations
deliver their typed result later, releasing the one-shot callable before user
code. The `uv::fs` callback facade unwraps submission status, cleaning the native
request and releasing owned state before throwing. C++ preparation and callable
allocation can still throw; raw filesystem submission is not generally `noexcept`.


## Raw watcher errors

`uv::raw::{signal,poll,fs_event}` start/stop methods return `status` for immediate
operational errors. Poll/fs-event callback errors are typed terminal outcomes:
stop the native source, release its callback slot, then deliver. Rejected starts
never install the new callback; an old callback survives only while its source
remains active. Signal/fs-event default construction is `noexcept` and
`init(loop&/loop_view) noexcept` returns native status; loop-taking constructors
unwrap the same result. The native loop pointer is the initialization sentinel,
with repeat init returning `UV_EALREADY` even during/after close. Pre-init starts,
stops, and queries return `UV_EBADF`; observations are false and close/ref/unref
are inert. Signal native init failures leave the wrapper unregistered and permit
retry without close. Poll also supports explicit descriptor/socket `init()`;
negative descriptors return `UV_EBADF` before native entry. Native failures are
returned unchanged after synchronous rollback of any incomplete registration,
leaving retryable uninitialized storage. Loop-taking constructors unwrap the same
result. Windows late-init rollback is documented in
[architecture](architecture.md#raw-poll-initialization-rollback). Native descriptor
and buffer-size queries return results; fs-event `path()` returns
`result<std::string>` and may throw on C++ allocation after checking initialization
and closing. See
[raw watchers](../user/raw-watchers.md) for the complete implemented contract.


Raw timer/idle/prepare/check operations follow the same explicit start/stop and
native-query policy. Timer `again` and `set_repeat` return `status`; start/stop
and timer reconfiguration reject closing handles with `UV_EBADF`. An inactive
timer's `again()` returns `UV_EINVAL` because terminal states release its callable. Duration
validation reports `UV_EINVAL` for non-finite or unrepresentable inputs before
changing native state or callbacks; finite negatives clamp to zero. Native repeat
and due-in getters carry unsigned chrono counts without an operational failure
channel. Timer/idle/prepare/check default construction is `noexcept`; explicit
`init()` returns status, rejecting repeated initialization (including after close) with
`UV_EALREADY`. Before initialization, operational methods return `UV_EBADF`,
observations return false/zero, and ref/unref/close are inert. Their loop-taking
constructors unwrap `init()` and throw. Runtime callable preparation retains its
separate throwing setup contract; see
[the user contract](../user/raw-watchers.md#timers).


Raw async `send() noexcept`, `set_callback`, and `set_callback_static` return
`uv::status`. Default construction is `noexcept`; all empty/runtime/static
`init(loop&/loop_view)` variants return native status. Loop-taking constructors
unwrap that result. Native init failure leaves retryable uninitialized storage;
callback preparation can throw before constructor/init/setter entry. Repeat init
reports `UV_EALREADY` without changing dispatch or subscription state. Pre-init
setters, queries, and send report `UV_EBADF`; close/ref are inert and observations
are false. Only setters check closing and return `UV_EBADF` on the loop thread. Cross-thread `send` checks the native
loop sentinel, then maps the native result. This pointer is immutable after init,
which must complete before publication and never overlap sends. The caller must
quiesce producers before close, so send during or after closing is a protocol violation rather than a promised recoverable state.
Notifications coalesce and carry no completion error/result payload. Empty
callback selection is supported and drops notifications while the handle remains
active. See [raw async notifications](../user/async.md).

Raw TCP setup now uses `init() -> status` with throwing constructors delegating to
`.throw_if_error()`. Open/bind/options/reset return status; sockname/peername return
`result<socket_address>` with inline payload storage. Common native descriptor and
buffer results preserve their payloads. `keep_alive(seconds)` enables keepalive and `disable_keep_alive()` disables it;
chrono delay validation reports
`UV_EINVAL` before mutation. Native init failure is already cleaned up by libuv;
uvpp restores its null loop sentinel without starting asynchronous close.

Normal close remains void, while reset rejection preserves stream subscriptions
and does not install its new close callback. Runtime callback construction and
address preparation can throw before operational entry. The inherited stream
and separate connect/write/shutdown request submission surfaces retain their
transitional throwing policy; see [raw TCP](../user/networking/raw-tcp.md).

Raw UDP receive control and send submissions now return statuses; completion uses
`(uv::raw::udp_send_request&, status)`. The send request shares the stream pending
guard and pre-delivery release protocol. Immediate sends and suffix batches retain
typed byte/datagram counts and would-block. Runtime callable and owning batch
preparation can throw C++ exceptions; those failures are not native errors. See
[raw UDP](../user/networking/raw-udp.md).
