# uvpp v2 Architecture

## Goal

uvpp v2 is a header-only C++ wrapper over libuv with a small compiled footprint and a deliberately C++ API. The library should keep libuv's performance model visible, but should not expose libuv structs as C++ base classes.

The v2 architecture treats libuv as the native engine and uvpp as a typed C++ facade around it.

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
docs/
  architecture.md
  api-principles.md
  callbacks.md
  error-handling.md
  ownership.md
  process.md
  migration-from-v1.md
  thread-safety.md
include/uvpp/
  uv.hpp
  core/
    error.hpp
    loop.hpp
    native.hpp
    callback.hpp
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
    write.hpp
    shutdown.hpp
    udp_send.hpp
    fs.hpp
  net/
    address.hpp
    buffer.hpp
  fs/
    file.hpp
    dir.hpp
    operations.hpp
tests/
examples/
```

The current v1 code remains a reference implementation and compatibility target only where useful. v2 is allowed to break API compatibility.

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

## User Data

libuv's `data` fields remain user-owned in v2. uvpp must not store `this` in `raw.data` for handles or requests.

Expose them through typed non-owning helpers:

```cpp
handle.user_data(&state);
handle.user_data(state);
auto* state = handle.user_data<state_type>();
handle.clear_user_data();
```

This API is intentionally zero-overhead: it stores exactly the native `void*` and performs only a typed cast at the boundary. It cannot validate that the requested type matches the stored object. The documentation should present it as a convenience over libuv's raw pointer, not as a type-safe ownership mechanism.

## Initial Vertical Slice

The first implementation should prove the architecture with:

- `loop`
- `basic_handle`
- `timer`
- `stream`
- `tcp`
- `write_request`
- `buffer_view`
- a TCP echo example
- focused tests for lifecycle, callbacks, errors, and basic TCP I/O

Once this slice is stable, add wrappers one libuv object family at a time.
