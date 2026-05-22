# uvpp v2 Architecture

## Goal

uvpp v2 is a header-only C++ wrapper over libuv with a small compiled footprint and a deliberately C++ API. The library should keep libuv's performance model visible, but should not expose libuv structs as C++ base classes.

The v2 architecture treats libuv as the native engine and uvpp as a typed C++ facade around it.

The project, repository, and include root keep the `uvpp` name. The public C++ API lives in namespace `uv`:

```cpp
#include <uvpp/uv.hpp>

uv::loop loop;
uv::tcp server(loop);
```

v2 does not provide a compatibility alias from `uvpp` to `uv`.

## Language Baseline

uvpp v2 requires C++20.

This is a design baseline, not an implementation accident. The API relies on C++20 vocabulary and language features such as `std::span`, `std::chrono` duration ergonomics, constrained zero-overhead callback APIs based on `template<auto Callback>`, and modern aggregate/value type conventions.

The project should not add compatibility shims for C++17 or earlier in the v2 core. If an older-standard compatibility layer is ever needed, it should live outside the primary API.

## Core Principles

- Prefer composition over inheriting from libuv C structs.
- Keep every native pointer conversion local and named through `native()` helpers.
- Represent libuv's object hierarchy in C++ with CRTP mixins, not with C++ inheritance from C structs.
- Make ownership and lifetime rules explicit.
- Keep handles non-copyable and non-movable once initialized.
- Support both zero-overhead callback forms and ergonomic runtime callables.
- Use modern C++ vocabulary types where they improve clarity without hiding libuv semantics.

## Directory Layout

```text
AGENTS.md
README.md
docs/
  index.md
  getting-started.md
  callbacks.md
  errors.md
  ownership-and-lifetime.md
  buffers.md
  streams.md
  filesystem.md
  network.md
  process.md
  design/
    architecture.md
    api-principles.md
    api-policy-decisions.md
    callback-strategy.md
    coroutine-strategy.md
    error-handling-strategy.md
    ownership-strategy.md
    request-guide.md
    thread-safety.md
include/uvpp/
  uv.hpp
  core/
    error.hpp
    loop.hpp
    native.hpp
    callback.hpp
    version.hpp
  handles/
    handle.hpp
    stream.hpp
    tcp.hpp
    pipe.hpp
    tty.hpp
    udp.hpp
    timer.hpp
    async.hpp
    prepare.hpp
    check.hpp
    idle.hpp
    signal.hpp
    poll.hpp
    process.hpp
    fs_event.hpp
    fs_poll.hpp
  requests/
    request.hpp
    connect.hpp
    dns.hpp
    fs.hpp
    write.hpp
    shutdown.hpp
    udp_send.hpp
  net/
    address.hpp
    buffer.hpp
    dns.hpp
    interface.hpp
    socket_address.hpp
  fs/
    file.hpp
    dir.hpp
    result.hpp
    operations.hpp
tests/
examples/
```

User documentation lives directly under `docs/`. Design and coding strategy lives under `docs/design/` and is referenced from `AGENTS.md`.

## Native Storage Model

Every wrapper owns or references a native libuv object through composition.

```cpp
template<class Derived, class Raw, class BaseRaw>
class native_storage {
public:
  using raw_type = Raw;

  Raw* native() noexcept { return &raw_; }
  const Raw* native() const noexcept { return &raw_; }

  BaseRaw* native_base() noexcept {
    return reinterpret_cast<BaseRaw*>(&raw_);
  }

protected:
  Raw raw_{};
};

template<class Derived, class Raw>
class basic_handle : public detail::native_storage<Derived, Raw, uv_handle_t> {
  // handle operations and callback slots
};
```

This keeps the wrapper layout predictable and avoids pretending that a C struct is a C++ base class.

The low-level wrappers must preserve one additional invariant: the native `raw_` object is the first member of a small standard-layout storage base. uvpp uses that invariant to reconstruct the C++ owner from a libuv callback pointer without consuming libuv's `data` field.

```cpp
static timer& timer::from_native(uv_timer_t* raw) noexcept;
```

This is deliberately a low-level implementation contract. It keeps callback trampolines allocation-free and leaves `uv_handle_t::data` / `uv_req_t::data` available to user code.

Required guard rails:

- `static_assert` the native storage base remains standard-layout;
- test that `Wrapper::from_native(wrapper.native())` returns the original wrapper;
- keep `raw_` private to the native storage base so derived wrappers cannot accidentally reorder it;
- do not add virtual functions or virtual bases to low-level wrappers;
- review every new wrapper family against this invariant before adding callbacks.

## C++ Hierarchy

libuv's C hierarchy is mirrored by C++ mixins:

```text
basic_handle<Derived, Raw>
  stream<Derived, Raw>
    tcp
    pipe
    tty
  udp
  timer
  async
  prepare
  check
  idle
  signal
  poll
  process
  fs_event
  fs_poll
```

`stream<Derived, Raw>` is a CRTP mixin that provides stream operations and a `native_stream()` helper. It does not create a separate runtime object.

## Native Interop

Every public wrapper should expose:

```cpp
Raw* native() noexcept;
const Raw* native() const noexcept;
```

Derived categories may expose additional typed native views:

```cpp
uv_handle_t* native_handle() noexcept;
uv_stream_t* native_stream() noexcept;
```

These functions are the only sanctioned locations for raw pointer reinterpretation.

Handles also expose an explicit non-owning view for code that operates on
generic libuv handles:

```cpp
uv::handle_view view = timer.view();
```

`handle_view` wraps a `uv_handle_t*` and offers the common handle operations
such as `active()`, `closing()`, `ref()`, and `unref()`. It is a borrowed view,
not an owner, and does not consume the native `data` field.

The conversion from a concrete handle wrapper to `handle_view` must stay named
as `.view()`. Do not provide an implicit conversion operator from
`basic_handle` to `handle_view`.

## Native Option Construction

Not every libuv struct should be wrapped like a handle or request. Handles and
requests have native identity: libuv stores their addresses and may refer to
them later from callbacks. Option structs such as `uv_process_options_t` are
different. They are temporary submission records consumed synchronously by the
libuv call.

When a native option struct contains borrowed pointers, prefer a C++ value that
owns the inputs and constructs the native view at submission time.

Example:

```cpp
auto options = uv::process_options::make("git")
  .arg("status")
  .cwd(repo);
```

`process_options` owns strings, argument vectors, environment entries, and stdio
containers. `process` builds a local `uv_process_options_t` immediately before
calling `uv_spawn()`. This avoids exposing a public `native()` view whose
pointers could be invalidated by later mutation, vector reallocation, copy, or
move of the owning C++ value.

Use a public native wrapper only when the native struct has a durable identity
or a stable value representation. Use a private or `detail` native view when the
native struct is only a transient adapter for one libuv call.

## User Data

libuv's `data` fields remain user-owned in v2. Wrapper objects must not store `this` in `raw.data` for handles or requests.

Expose them through typed non-owning helpers:

```cpp
handle.user_data(&state);
handle.user_data(state);
auto* state = handle.user_data<state_type>();
handle.clear_user_data();
```

This API is intentionally zero-overhead: it stores exactly the native `void*` and performs only a typed cast at the boundary. It cannot validate that the requested type matches the stored object. The documentation should present it as a convenience over libuv's raw pointer, not as a type-safe ownership mechanism.

## Wrapper Growth

Add wrappers one libuv object family at a time. Each addition should preserve the same low-level contract:

- address-stable native storage;
- explicit native accessors;
- no wrapper-owned state in libuv `data`;
- typed callback arguments;
- explicit async lifetime;
- focused tests for layout, lifecycle, errors, and callback behavior.

## Loop Run Return Value

`loop::run()` and `loop_view::run()` take `uv::run_mode` and return `bool`. The return value is `true` if there are still active handles or requests pending after the loop exits (i.e., when run in `uv::run_mode::nowait` or `uv::run_mode::once` mode); it is `false` when the loop is empty. In the default `uv::run_mode::until_done` mode the loop runs until there is no more work and always returns `false`.

## Loop Introspection

`loop` and `loop_view` expose thin wrappers over libuv loop introspection:

- `backend_fd()` and `backend_timeout()` report backend polling details;
- `enable_metrics_idle_time()`, `metrics_idle_time()`, and `metrics_info()` expose libuv loop metrics;
- `configure_block_signal()` and `fork()` map to the corresponding libuv loop configuration operations;
- `walk(callback)` visits handles synchronously as `handle_view` values;
- `handles()` collects the current walked handles into a `std::vector<handle_view>` for range-based loops and ranges pipelines.

These APIs intentionally stay close to libuv. They do not take ownership of
handles and do not add wrapper state.

`backend_timeout()` returns `std::optional<std::chrono::milliseconds>`:
`std::nullopt` represents libuv's infinite wait sentinel. `metrics_idle_time()`
returns `std::chrono::nanoseconds`. Loop counters and event counters remain
plain integer counts in `loop_metrics`.

## Filesystem Layering

Filesystem operations are intentionally split from normal request wrappers.

`uv::fs` is the public C++ layer: it owns the internal `uv_fs_t`, performs cleanup automatically, and returns scalar or owned result values.

`uv::fs::raw` is the direct libuv-facing layer: it exposes `raw::request`, manual cleanup, request reuse, caller-owned buffers, request-scoped result views, and static callbacks.

This is the preferred pattern when libuv exposes a protocol that cannot be made safe with a thin wrapper alone: keep the raw protocol available, but do not make it the default public API.

`uv::fs::raw` is not a precedent for moving every low-level operation into a
global raw namespace. It exists because filesystem has two complete API layers
with different ownership rules. Handle APIs such as TCP and UDP are already the
low-level layer; individual native-shaped operations should stay on the relevant
handle unless a higher-level layer is introduced above them.

## Libuv Feature Availability

uvpp compiles against the user's installed libuv headers. Newer libuv constants
and functions must therefore be guarded when they are not available in older
commonly used packages.

Feature checks live in `include/uvpp/core/version.hpp` as named
`UVPP_HAS_*` capability macros. Public headers should depend on those macros,
not on repeated numeric `UV_VERSION_HEX` comparisons. This keeps version policy
centralized and gives users a stable compile-time spelling for portable code.

The capability macros are compile-time API availability checks. If a native
symbol is absent from the libuv headers, uvpp omits the corresponding wrapper
API. It does not declare a replacement function that fails at runtime, because
that would require spelling or emulating a native function that the installed
headers do not provide.
