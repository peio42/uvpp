# API Policy Decisions

This file records small but important API policy decisions that should stay
consistent as uvpp grows. It is intentionally more concrete than the general
API principles.

## `try_*` Names

Reserve `try_*` for non-throwing variants of APIs that would otherwise throw on
immediate libuv failure.

`try_*` functions should return `std::error_code` or a similarly explicit
non-throwing status channel, and should not throw for normal immediate failure.

Example:

```cpp
std::error_code close_error = loop.try_close();
```

Do not use `try_*` just because the underlying libuv function contains `try` in
its name.

## Immediate Non-Blocking Operations

When libuv exposes a synchronous "try now" operation, name the uvpp wrapper
`*_now()`.

Examples:

```cpp
auto sent = udp.send_now(buffer, address);
auto written = stream.write_now(buffer);
```

These functions do not own asynchronous state, do not take request objects, and
do not take callbacks.

If a non-blocking "nothing happened" status is part of the normal control flow
for that operation, model it explicitly in a typed result object instead of
throwing. For example, an operation based on `uv_try_write()` should expose a
`would_block()` branch for `UV_EAGAIN`.

Immediate low-level wrappers must not allocate hidden operation metadata. If a
libuv immediate operation needs temporary arrays or scratch storage, require the
caller to provide those arrays or introduce an explicitly owning higher-level
value. Do not build `std::vector` or similar storage inside the immediate
wrapper.

Example: `udp::send_many_now()` maps to `uv_udp_try_send2()`, whose native shape
requires parallel arrays of buffer pointers, buffer counts, and destination
addresses. The low-level uvpp API accepts an explicit borrowed batch view over
caller-provided arrays. `udp_send_batch` is the higher-level value that owns
that metadata while still borrowing payload bytes and destination addresses.
The low-level `*_now()` operation itself stays allocation-free.

## Typed Result Objects

Use a typed result object when a libuv return value contains meaningful payload
and status in the same integer.

Examples:

- stream reads: positive values are byte counts, `UV_EOF` is a normal EOF branch;
- synchronous non-blocking writes: positive values are byte counts, `UV_EAGAIN`
  means the operation would block.

Typed result objects should make ordinary branches visible:

```cpp
if (result.would_block()) {
  return;
}

if (result.has_error()) {
  auto ec = result.error_code();
  return;
}

auto bytes = result.bytes_written();
```

Expose raw status for interop when useful, but avoid forcing callers to decode
libuv sentinel values for common control flow.

## Blocking Operations

When libuv exposes a synchronous operation that blocks the current thread, make
that behavior explicit in the uvpp name unless the blocking semantics are
already obvious from the domain.

`uv_sleep()` is the main naming-sensitive case. A wrapper for it should not be
named `sleep_for`, because `sleep_for(loop, ...)` is reserved for a future
event-loop-friendly coroutine/timer helper. Prefer a name such as
`sleep_blocking_for(duration)` for the `uv_sleep()` wrapper.

Document that blocking helpers stop the current thread from running callbacks.
Calling them on the same thread that is expected to call `loop.run()` prevents
that loop from dispatching timers, I/O, and other events until the blocking call
returns.

## Borrowed Views

Borrowed views must be produced by named functions, not implicit conversion
operators.

Examples:

```cpp
uv::handle_view handle = timer.view();
uv::buffer_view buffer = storage.view();
```

The `.view()` spelling makes the ownership relationship visible: the returned
object does not own the underlying native handle, bytes, or storage.

Do not add implicit conversions from owning or address-stable wrappers to
borrowed views such as `handle_view`.

## Typed Constant Domains

Use namespace-level `enum class` types for compact libuv constant domains that
are part of ordinary uvpp usage.

Examples:

```cpp
loop.run(uv::run_mode::once);

if (handle.type() == uv::handle_type::tcp) {
}
```

Prefer namespace-level types over nested types when the concept is shared by
multiple wrappers or views. For example, `run_mode` is used by both `loop` and
`loop_view`, and `handle_type` is used by `handle_view` and pipe pending-handle
inspection.

Use `type()` for the primary classification of the object being inspected. A
request wrapper's `type()` returns `uv::request_type`. More specific
classification should use a semantic name instead of hiding the primary type;
for example, `uv::fs::raw::request::operation()` returns `uv::fs_type`.

Do not add raw constant overloads mechanically. Keep raw libuv constants as an
implementation detail unless the API needs a real interop escape hatch.

## Native Access

Raw libuv access stays explicit through named helpers:

```cpp
uv_tcp_t* raw = tcp.native();
uv_stream_t* stream = tcp.native_stream();
uv_handle_t* handle = tcp.native_handle();
```

Do not add implicit conversion operators to raw libuv pointers.

## Version-Gated Libuv Features

uvpp is header-only and compiles against the libuv headers available to the
consumer. Public wrappers for libuv APIs or constants introduced after the
project's practical baseline must be gated at compile time.

Use named capability macros from `uvpp/core/version.hpp` instead of repeating
raw `UV_VERSION_HEX` comparisons in feature headers or tests:

```cpp
#if UVPP_HAS_UDP_TRY_SEND2
auto result = udp.send_many_now(batch);
#endif
```

The macro name should describe the libuv capability, not the wrapper spelling.
For example, prefer `UVPP_HAS_UDP_TRY_SEND2` over
`UVPP_HAS_UDP_SEND_MANY_NOW`.

Apply the same capability macro to:

- public types that depend on a newer libuv constant;
- public member functions that call a newer libuv function;
- tests that reference the gated API;
- documentation examples when the feature may not exist for all supported
  libuv packages.

Do not provide a stub member that compiles but always returns "unsupported" when
the underlying declaration is absent. If the libuv header cannot declare the
native function or constant, the uvpp wrapper should not declare that specific
API either. Users can test the `UVPP_HAS_*` macro when writing portable code.

Prefer documented libuv introduction versions. When the docs only describe a
behavior change and not the original symbol introduction, use the earliest
version that is known to expose the symbol and keep the decision centralized in
`version.hpp`.

## Public Template Constraints

Use C++20 concepts for public templates that depend on structural API
contracts.

Examples:

```cpp
template<class T>
concept stream_handle = requires(T& handle) {
  { handle.native_stream() } -> std::convertible_to<uv_stream_t*>;
};

template<stream_handle Client>
void accept(Client& client);
```

Good candidates are APIs where the template argument represents a user-visible
category, such as stream-like handles, callbacks passed to `walk()`, or wrapper
types passed to `handle_view::as<T>()`.

Do not add constraints mechanically to every implementation template. Private
helpers such as submit lambdas, result factories, and local getter utilities can
remain unconstrained when their use is obvious and diagnostics are already
local.

## Chrono For Durations

Use `std::chrono` for durations in public APIs.

If libuv represents a timeout with a sentinel value, model the sentinel in the
type instead of leaking it directly when practical. For example,
`loop.backend_timeout()` returns `std::optional<std::chrono::milliseconds>`,
where `std::nullopt` represents an infinite wait.

Counters remain integer counts. Do not wrap event counts or loop iteration
counts in duration types.

## Fluent Option Values

Use fluent option construction only when a value has enough independent fields
to benefit from named, chained configuration. Good candidates own strings,
vectors, native escape hatches, and compact flags that would otherwise make call
sites noisy.

Example:

```cpp
auto options = uv::process_options::make("git")
  .arg("status")
  .cwd(repo)
  .inherit_stdout()
  .inherit_stderr();
```

When the chain produces the final option value directly, prefer `Type::make()`
over a separate public builder type. A separate builder should exist only when
the intermediate object has a distinct invariant or lifetime from the final
value.

Do not add fluent builders mechanically to small parameter groups. For example,
filesystem `open` flags and modes are already a compact native vocabulary, so a
builder should be added only if a higher-level filesystem layer needs clearer
semantics than raw POSIX-style flags can provide.

Fluent helpers may coexist with public fields when the value remains a low-level
configuration object. Use clear storage names when a helper needs an idiomatic
method name, such as `.cwd(...)` writing to `working_directory`.

## Range-Shaped Results

When a result naturally represents a collection, expose a range-friendly shape
with `begin()` / `end()` or a named range-returning helper. Prefer ranges when
they remove manual index or `next()` loops without hiding ownership or
lifetime.

Examples:

```cpp
for (uv::handle_view handle : loop.handles()) {
}

for (auto entry : scandir_result) {
}

for (auto entry : readdir_result.entries(buffer)) {
}
```

Borrowed and single-pass ranges are acceptable when they model the underlying
libuv protocol. The type or documentation must make the lifetime and traversal
rules clear. Do not materialize a vector just to satisfy range syntax in a raw
API; copying belongs in higher-level APIs that explicitly own their results.

## Async Lifetime Visibility

If an operation outlives the initiating call, the owner of the operation state
must be clear in the function signature.

Low-level request-shaped API:

```cpp
uv::write_request request;
stream.write(request, buffer, callback);
```

Synchronous immediate API:

```cpp
auto result = stream.write_now(buffer);
```

Do not hide request lifetime behind a low-level convenience that accepts only
data and a callback unless the API name and documentation make ownership
explicit.

## Loop and Loop View Overloads

Every free function that accepts a loop must provide two overloads: one taking
`loop_view` and one taking `loop&`. The `loop&` overload forwards to the
`loop_view` overload via `.view()`.

```cpp
inline void getaddrinfo(loop_view loop, getaddrinfo_request &request, ...);

inline void getaddrinfo(loop &loop, getaddrinfo_request &request, ...) {
  getaddrinfo(loop.view(), request, ...);
}
```

This ensures callers who hold a `loop&` do not need to call `.view()` manually,
while the implementation stays in the `loop_view` overload. Apply the same
pattern to every argument combination that produces additional overloads (for
example, `nullptr_t` vs `string_view` variants for getaddrinfo).
