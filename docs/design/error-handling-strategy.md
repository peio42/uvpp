# Error Handling

This document describes the implemented common contracts in
[core/error.hpp](../../include/uvpp/core/error.hpp) and the remaining specialized
domain results. Broader `uv::ops` adaptation is tracked in
[proposal 006](../proposals/006-errors-and-results.md).

## V3 operation surfaces

Implemented high-level network awaits throw native operational failures at the
await expression, including submission failures. `uv::ops::close` reports the same
owner-close failures as `uv::status`. Broader paired operations remain proposed.
Raw explicit operational results are the target; the low-level submission helpers
below have not yet completed that migration. C++ setup failures need separate
contracts and result-oriented does not imply `noexcept`.

Task promises capture exceptions; child awaits and completed spawn-handle result
observation rethrow them. Current wrong-loop owner awaits raise `std::logic_error`;
active spawn-handle destruction terminates. These are implementation limits, not
a claim that all proposal error/retention rules are implemented.

## Existing low-level failures and exceptions

Existing low-level submission APIs throw `uv::error` on a negative native submission result.
`uv::error` derives from `std::system_error`; the libuv category formats messages
with `uv_strerror`, while the exception constructor adds the `uv_err_name` context.
The category's name is `libuv`. `uv::error_code` is the public operational-error
value: `native()` returns zero or a negative libuv status, and explicit conversion
produces the equivalent `std::error_code`. Nonnegative values map to an empty
`uv::error_code`.

`throw_if_error(int)` returns the unchanged integer on success and throws on a
negative value. Its return value is used, for example, to preserve `uv_run()`'s
nonzero result. Submission setup can also throw standard exceptions from string,
vector, or callback storage allocation and explicit argument validation.

Non-throwing variants exist selectively: `loop.try_close()` and supported request
`try_cancel()` methods return `uv::error_code`. There is no `tcp.try_bind()` in the current code. Synchronization attempts such as `mutex.try_lock()` use standard attempt
semantics and can throw on unexpected native errors; see
[API policy](api-policy-decisions.md).

## Asynchronous Completion

A native submission failure is reported by the initiating call; a later failure
is reported by a completion callback. These remain distinct channels in the low-level callback implementation.

```cpp
tcp.connect(req, addr, [](uv::connect_request&, uv::status status) {
  if (!status) {
    auto ec = status.error();
    (void)ec;
  }
});
```

`uv::result<T>` stores either one `T` or one `uv::error_code`; `uv::result<void>`
stores success or an operational error. `uv::status` is its alias when the
declaration names an error-only completion. Both expose `has_value()`, explicit
`operator bool()`, `value()`, and `error()`. `value()` throws `uv::error` on an
error result; `error()` is empty for success. `result<T>` has the usual lvalue and
rvalue accessors and supports move-only `T` without an allocation.

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
| Stream/UDP read results | `status()` returns `uv::status`; `count()` retains native byte count/status | `status().error()`; no direct `error_code()` or `raw_status()` |
| `fs_event_result`, `fs_poll_result` | `status()` returns `uv::status` | `status().error()`; no direct `error_code()` or `raw_status()` |
| `poll_result` | `status()` returns `uv::status` | Direct `error_code()`, no `raw_status()`; `raw_events()` is the event mask |

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
