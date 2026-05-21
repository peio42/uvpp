# API Principles

For concrete naming and result-shape decisions that must stay consistent across
future API additions, also read [API policy decisions](api-policy-decisions.md).

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

## Typed Constants

Ordinary user-facing APIs should not require libuv constants when the constant
domain is small, stable, and has clear C++ semantics. Prefer thin `enum class`
types in namespace `uv`:

```cpp
loop.run(uv::run_mode::nowait);

if (handle.type() == uv::handle_type::timer) {
  // timer handle
}
```

These enums map directly to libuv constants and should not store extra state or
add runtime overhead. Keep the names semantic rather than transliterating the C
spelling:

- `*_mode` for exclusive modes, such as `run_mode`;
- `*_type` for classification results, such as `handle_type`, `request_type`,
  and `fs_type`;
- `*_event` or `*_event_kind` for observed events;
- `*_flag` for configuration bitmasks.

Do not add native constant overloads by default. A raw escape hatch is useful
only when there is a concrete libuv interop use case that the typed API cannot
express cleanly. Low-level wrappers may still expose raw objects through
`native()`, `native_handle()`, `native_stream()`, or `native_request()`.

## Explicit Borrowed Views

Borrowed views must be produced with named functions, not implicit conversion
operators.

Examples:

```cpp
uv::handle_view handle = timer.view();
uv::buffer_view buffer = storage.view();
```

The `.view()` spelling makes the lifetime relationship visible at the call
site: the returned object does not own the underlying native handle, buffer, or
storage. This is especially important for asynchronous operations and loop
introspection, where keeping a view after the owner is closed or destroyed is a
user lifetime error.

Do not add implicit conversions from owning or address-stable wrappers to
borrowed views such as `handle_view`. A named conversion is slightly longer, but
it preserves uvpp's explicit ownership model.

## Thin Native Operations

Low-level wrappers may expose immediate libuv operations directly when they do not change ownership semantics.

Examples:

```cpp
tcp.no_delay(true);
tcp.keep_alive(true, 60);
tcp.simultaneous_accepts(true);

auto fd = tcp.fileno();
auto bytes = stream.write_queue_size();

udp.connect(ipv4{"127.0.0.1", 1234});
pipe.pending_instances(4);
```

These functions should remain thin: validate through libuv, throw `uv::error` on immediate failure, and avoid storing extra state in the wrapper.

For immediate operations that need caller-provided native metadata, keep the raw
shape visible instead of hiding allocation in the wrapper. The operation should
still live on the relevant handle when it is semantically a handle operation.
For example, UDP batch send remains `udp.send_many_now(...)`, but the low-level
argument is an explicit borrowed batch view over libuv-compatible arrays.

Do not move isolated raw-shaped handle operations into a global `uv::raw`
namespace just because their arguments are close to libuv. Use a separate raw
namespace only when a whole sub-domain has a distinct ownership and lifetime
model, as filesystem does with `uv::fs::raw`.

## User Data

The native `data` field is reserved for the application, not for wrapper internals.

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
- `owned_buffer` owns bytes and can explicitly produce a `buffer_view` with `view()`;
- `std::span<std::byte>` / `std::span<const std::byte>` are accepted for generic byte ranges where `uv_buf_t` compatibility is not required at the call site.

A single buffer type must not sometimes own memory and sometimes only view it. Ownership is represented by the type name.

Example:

```cpp
owned_buffer storage{4096};
buffer_view view = storage.view();
```

The conversion is intentionally named rather than implicit. Producing a `buffer_view` creates a borrowed view into the owned storage, and async libuv operations still require the storage to outlive the operation.

## Bitmask Options

Where libuv exposes compact bitmasks, v2 should prefer thin typed helpers without hiding the native model.

`poll_event` is the current pattern:

```cpp
poll.start(poll_event::readable | poll_event::disconnect, callback);

if (has_poll_event(events, poll_event::readable)) {
  // fd is readable
}
```

The wrapper still accepts raw `int` events for native interop. The enum helpers document common flags and avoid spelling libuv constants in ordinary C++ call sites.

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

Filesystem operations use this second shape by default. `uv::fs` owns the internal request and buffers/results needed for safe callback delivery, while `uv::fs::raw` exposes the manual libuv request protocol for callers that explicitly want it.
