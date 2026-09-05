# Error Handling

This document describes the current contracts in
[core/error.hpp](../../include/uvpp/core/error.hpp) and the domain result classes.
Uniform future result adaptation is tracked in
[proposal 006](../proposals/006-errors-and-results.md).

## Immediate Failures and Exceptions

Primary submission APIs throw `uv::error` on a negative native submission result.
`uv::error` derives from `std::system_error`; the libuv category formats messages
with `uv_strerror`, while the exception constructor adds the `uv_err_name` context.
The category's name is `libuv`. Nonnegative values map to an empty error code.

`throw_if_error(int)` returns the unchanged integer on success and throws on a
negative value. Its return value is used, for example, to preserve `uv_run()`'s
nonzero result. Submission setup can also throw standard exceptions from string,
vector, or callback storage allocation and explicit argument validation.

Non-throwing variants exist selectively: `loop.try_close()` and supported request
`try_cancel()` methods return `std::error_code`. There is no `tcp.try_bind()` in
v2. Synchronization attempts such as `mutex.try_lock()` use standard attempt
semantics and can throw on unexpected native errors; see
[API policy](api-policy-decisions.md).

## Asynchronous Completion

A native submission failure is reported by the initiating call; a later failure
is reported by a completion callback. These are distinct channels in v2.

```cpp
tcp.connect(req, addr, [](uv::connect_request&, uv::result status) {
  if (!status) {
    auto ec = status.error_code();
    (void)ec;
  }
});
```

The base `uv::result` is not a template. It exposes `ok()`, explicit `operator
bool()`, `canceled()`, integer `status()`, and `error_code()`. It has no
`raw_status()` or `error()` member.

## Result Families

The current result API is not uniform across all families:

| Family | Status access | Error access and payload |
| --- | --- | --- |
| `uv::result` | `status()` returns `int` | `error_code()`, `canceled()` |
| DNS and random results | `status()` returns `uv::result`; `raw_status()` returns `int` | Direct `error_code()` plus family payload |
| Public and raw filesystem results | `status()` returns `uv::result`; `raw_status()` returns zero on success or a negative error | Direct `error_code()`; `raw()` retains native payload/status value |
| Stream/UDP read results | `status()` returns `uv::result`; `count()` retains native byte count/status | `status().error_code()`; no direct `error_code()` or `raw_status()` |
| `fs_event_result`, `fs_poll_result` | `status()` returns `uv::result` | `status().error_code()`; no direct `error_code()` or `raw_status()` |
| `poll_result` | `status()` returns `uv::result` | Direct `error_code()`, no `raw_status()`; `raw_events()` is the event mask |

These families expose `ok()` and explicit boolean conversion. Use the payload
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
result as an I/O failure. `status().error_code()` still maps `UV_EOF` to a code;
v2 does not throw it automatically. Filesystem read EOF is a successful zero-byte
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
