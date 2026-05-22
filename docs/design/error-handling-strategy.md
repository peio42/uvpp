# Error Handling

## Goals

uvpp v2 should make libuv errors explicit and consistent. The library may offer both throwing and non-throwing APIs, but each call site should make the chosen behavior clear.

The grammar is:

- immediate libuv submission errors throw `uv::error` in the primary low-level API;
- optional non-throwing immediate APIs use the `try_` prefix and return `std::error_code`;
- asynchronous callback statuses are delivered as `uv::result`;
- `uv::result` exposes `status()` and `error_code()`, not `error()`;
- v2 does not use `result<void>` in the low-level API.

See [API policy decisions](api-policy-decisions.md) for the related naming rule:
libuv "try now" operations should use `*_now()` in uvpp rather than consuming
the `try_*` prefix.

## Error Type

Use a small `uv::error` type for exceptions and expose `std::error_code` for non-throwing APIs.

```cpp
class error : public std::system_error {
public:
  explicit error(int uv_status);
};
```

The error category should map libuv status codes to messages through `uv_strerror` and names through `uv_err_name`.

## Throwing API

The default low-level wrapper may throw on immediate libuv failures:

```cpp
void throw_if_error(int status) {
  if (status < 0) {
    throw error(status);
  }
}
```

Throwing APIs keep immediate failure handling concise, but callback boundaries need special care.

## Non-Throwing API

Expose non-throwing variants only where they materially improve control flow:

```cpp
std::error_code tcp.try_bind(ipv4 addr) noexcept;
```

Do not mix throwing and non-throwing behavior in the same function based on runtime state.

## Callback Status

Callback status values should be converted into explicit result objects rather than being thrown through libuv.

Example:

```cpp
tcp.connect(req, addr, [](connect_request& req, result r) {
  if (!r) {
    // handle r.error_code()
  }
});
```

Throwing from inside the user callback is a separate concern and must be handled by the callback exception policy.

## EOF

EOF is not an exceptional error for streams. It should have an explicit branch in `read_result`.

```cpp
if (result.eof()) {
  stream.close();
}
```

## Immediate Failure vs Completion Failure

libuv can fail immediately when submitting an operation, or later in the completion callback.

v2 must handle both:

- immediate failures are returned or thrown by the initiating function;
- completion failures are delivered through callback result objects.

This distinction should be covered by tests for every request family.

## Result Object Interface Contract

Every typed result object that reports asynchronous completion status must expose
the following interface:

```cpp
bool ok() const noexcept;
explicit operator bool() const noexcept { return ok(); }
result status() const noexcept;
int raw_status() const noexcept;
std::error_code error_code() const noexcept;
```

`ok()` returns `true` when the status value is non-negative (i.e., libuv
considers the operation successful). `status()` wraps the raw integer in
`uv::result`. `raw_status()` is available for interop. `error_code()` converts
to a `std::error_code` via the libuv error category.

Do not expose a direct `error()` accessor that throws or returns `uv::error` on
the result object. Callers who need to throw should test `ok()` and throw
themselves, or use `throw_if_error(result.raw_status())`.

Result objects that carry payload beyond status follow the same pattern and add
payload accessors (`hostname()`, `bytes()`, etc.) alongside the status members.

Owned result types (those that manage a heap-allocated libuv resource, such as
`getaddrinfo_result`) must be move-only. Borrowed result types (those that hold
views into caller or libuv storage, such as `read_result`) may use value
semantics if the borrow rules are documented.

## Default Policy

Initial v2 should use:

- throwing functions for immediate failures;
- result objects for asynchronous callback status;
- `std::terminate` if a user callback throws.

This keeps the first implementation simple and explicit.

A future loop-level exception handler may be added later, but it is not part of the initial policy. If introduced, it must be explicit on `loop`/`loop_view` and must not silently change the behavior of existing callback trampolines.
