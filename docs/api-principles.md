# API Principles

## API Shape

uvpp v2 should feel like a C++ library that happens to use libuv, not a direct transliteration of libuv naming and memory conventions.

The public API should use:

- C++20 as the minimum language standard.
- `std::string_view` for read-only text inputs.
- `std::span` for buffer ranges and non-owning byte views.
- `std::chrono` for durations.
- `std::error_code` where non-throwing APIs are exposed.
- RAII where it accurately models ownership.

The public API should avoid:

- exposing libuv fields as normal user-facing state;
- requiring users to write casts;
- requiring users to know every exact libuv callback typedef;
- hiding asynchronous lifetime requirements behind misleading destructors.

## Naming

Use C++ style names consistently.

Examples:

```cpp
loop.run();
timer.start(100ms, 1s, callback);
tcp.bind(ipv4{"0.0.0.0", 2345});
stream.read_start(...);
handle.close();
```

Prefer clear names over one-to-one libuv names when the C name is awkward. Keep libuv terminology where it is already clear and familiar, such as `listen`, `accept`, `bind`, `connect`, `close`, and `run`.

## Header-Only Design

v2 remains header-only and C++20-only. Implementation details should live in headers under `detail/` only when needed.

Suggested conventions:

- Keep public class definitions in focused headers.
- Keep trampoline and callback machinery in `core/callback.hpp`.
- Keep native conversion utilities in `core/native.hpp`.
- Put broadly included, low-level headers in `core/`.
- Avoid including the aggregate `uv.hpp` from implementation headers.

## Native Access

Raw libuv access is supported but explicit:

```cpp
uv_tcp_t* raw = tcp.native();
uv_stream_t* stream = tcp.native_stream();
```

There should be no implicit conversion operator to raw libuv pointers in the primary API. Implicit conversions make call sites short, but they reintroduce the same ambiguity v2 is meant to remove.

## User Data

The native `data` field is reserved for the application, not for uvpp internals.

Expose it as a typed non-owning pointer API:

```cpp
struct session_state {};

session_state state;
client.user_data(state);

auto* current = client.user_data<session_state>();
client.clear_user_data();
```

The typed getter is a cast convenience, not a type-safe container. Storing `session_state` and retrieving `other_state` is a programmer error and cannot be diagnosed by the compiler because libuv stores only `void*`.

Do not add the user data type to the primary handle/request templates. A design such as `tcp<session_state>` would make the whole hierarchy contagious, complicate APIs that only need "some tcp", and produce more template instantiations without improving the native storage model.

If stronger ownership or type safety is needed later, provide an opt-in higher-level layer on top of the low-level wrappers. The low-level API should remain a zero-overhead view over libuv's `data` pointer.

## Value Types

Small value wrappers such as `buffer_view`, `ipv4`, `ipv6`, and `timespec` can use composition while preserving native layout with `static_assert`.

Example:

```cpp
class buffer_view {
public:
  std::span<std::byte> bytes() noexcept;
  uv_buf_t* native() noexcept;

private:
  uv_buf_t raw_{};
};
```

Buffer taxonomy is fixed:

- `buffer_view` is a non-owning `uv_buf_t`-compatible view;
- `owned_buffer` owns bytes and can produce a `buffer_view`;
- `std::span<std::byte>` / `std::span<const std::byte>` are accepted for generic byte ranges where `uv_buf_t` compatibility is not required at the call site.

A single buffer type must not sometimes own memory and sometimes only view it. Ownership is represented by the type name.

## Minimal Surprises

The API should make asynchronous lifetime visible. If a method starts an operation that outlives the call, its request object or callback state must have a clear owner.

Bad shape:

```cpp
tcp.write(data); // unclear where request and data live
```

Better shape:

```cpp
write_request req;
tcp.write(req, data, callback);
```

Or a higher-level convenience that clearly owns the operation state:

```cpp
tcp.async_write(data, callback);
```
