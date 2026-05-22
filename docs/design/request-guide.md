# Adding a New Request Type

This guide lists every step required to add a well-formed request to uvpp.
Follow it in order; each step is load-bearing for the standard-layout invariant,
the callback contract, and the public API surface.

## 1. Request class and result types — `include/uvpp/requests/<domain>.hpp`

Create a header in `include/uvpp/requests/`. It should contain:

- one or more **typed result classes** (see the result contract below);
- one **request class** per libuv request type, inheriting `basic_request<Derived, uv_foo_t>`.

### Result class contract

Every result object must expose:

```cpp
bool ok() const noexcept;
explicit operator bool() const noexcept;
result status() const noexcept;
int raw_status() const noexcept;
std::error_code error_code() const noexcept;
// payload accessors …
```

Result objects that own a heap-allocated libuv resource (such as `addrinfo*`)
must be **move-only**. Result objects that hold borrowed views may use value
semantics when the borrow rules are documented.

### Request class contract

```cpp
class foo_request final : public basic_request<foo_request, uv_foo_t> {
public:
  using callback = std::function<void(foo_request&, foo_result)>;

  void set_callback(callback cb);

  // Called by the trampoline. Must be noexcept.
  void invoke(int status, ...) noexcept;

  // Static callback path. Must be noexcept.
  template<auto Callback>
  void invoke_static(int status, ...) noexcept;

private:
  callback callback_{};
  // Owned copies of submission inputs, if libuv borrows pointers from *this
};
```

#### `invoke()` rules

1. Extract and clear the callback slot before calling it:
   `auto cb = std::move(callback_); callback_ = {};`
2. Free any libuv-owned resources even when no callback is present (e.g.
   `uv_freeaddrinfo(addresses)` when `!cb`).
3. Clear owned submission inputs before returning (`clear_inputs()`).
4. Mark the method `noexcept`. Exceptions from user code are caught by
   `detail::invoke_callback` and forwarded to `std::terminate`.

## 2. Submission helpers — `include/uvpp/net/<domain>.hpp` (or appropriate domain)

Create a header that contains the free functions users call to submit the
operation. Separate the dispatch layer from the request/result definitions so
callers who only need the result types can include the thinner header.

### Trampoline

Place the libuv C callback in the local `detail` namespace and name it
`<operation>_trampoline`:

```cpp
namespace detail {
  inline void foo_trampoline(uv_foo_t *raw, int status) noexcept {
    foo_request::from_native(raw).invoke(status);
  }
}
```

### `loop` / `loop_view` overload pair

Every public free function must have two overloads. The `loop&` overload
forwards to the `loop_view` overload:

```cpp
inline void foo(loop_view loop, foo_request &request, ..., foo_request::callback cb);
inline void foo(loop &loop, foo_request &request, ..., foo_request::callback cb) {
  foo(loop.view(), request, ..., std::move(cb));
}
```

Reproduce the pair for every additional argument combination that produces
distinct overloads (e.g. `nullptr_t` vs `string_view` variants).

### Static callback overloads

Provide `foo_static<Callback>()` variants that take no runtime callable:

```cpp
template<auto Callback>
void foo_static(loop_view loop, foo_request &request, ...);

template<auto Callback>
void foo_static(loop &loop, foo_request &request, ...) {
  foo_static<Callback>(loop.view(), request, ...);
}
```

### When `detail::submit_request` applies

`detail::submit_request` from `requests/request.hpp` has two overloads.

**3-argument form** — use when all submission inputs are owned by the caller
(buffer views, address pointers, scalars) and libuv borrows them for the
duration of the operation:

```cpp
detail::submit_request(request, std::move(callback),
  [&] { return uv_foo(loop.native(), request.native(), trampoline, buffer, addr); });
```

**4-argument form** — use when the request must store copies of submission
inputs (for example, strings copied to provide null-termination for libuv's
`const char*` parameters). Caller must call `set_inputs()` before this helper;
the `on_error` lambda is invoked alongside the callback rollback on immediate
failure:

```cpp
request.set_inputs(node, service, hints);
detail::submit_request(request, std::move(callback),
  [&] { return uv_getaddrinfo(loop.native(), request.native(), trampoline,
                              request.node_arg(), request.service_arg(),
                              request.hints_arg()); },
  [&] { request.clear_inputs(); });
```

For **static callback paths** where no runtime callback is stored, use
`detail::submit_with_rollback` instead:

```cpp
request.set_inputs(node, service, hints);
detail::submit_with_rollback(
  [&] { return uv_getaddrinfo(loop.native(), request.native(), static_trampoline,
                              request.node_arg(), request.service_arg(),
                              request.hints_arg()); },
  [&] { request.clear_inputs(); });
```

## 3. Update `include/uvpp/uv.hpp`

Add includes for both new headers in dependency order (request definitions before
dispatch functions):

```cpp
#include "uvpp/requests/foo.hpp"
#include "uvpp/net/foo.hpp"
```

## 4. Update `include/uvpp/core/version.hpp` if needed

If the request wraps a libuv API added after the project baseline, add a named
capability macro:

```cpp
#define UVPP_HAS_FOO UVPP_UV_VERSION_AT_LEAST(1, X, 0)
```

Guard the public header and any tests with that macro. See
[api-policy-decisions.md](api-policy-decisions.md) for the full version-gating
policy.

## 5. Tests

### Layout test — `tests/test-layout.cpp`

Add `static_assert` and round-trip reconstruction assertions alongside the
existing request layout checks:

```cpp
static_assert(std::is_standard_layout_v<uv::foo_request::native_storage>);

uv::foo_request foo;
EXPECT_EQ(&foo, &uv::foo_request::from_native(foo.native()));
EXPECT_EQ(&foo, &uv::foo_request::from_native(foo.native_request()));
```

### Functional tests — `tests/test-<domain>.cpp`

Cover at minimum:

- a successful async completion with a runtime callback;
- a static callback compilation check (take a function pointer to the static
  overload);
- an immediate submission failure that throws `uv::error` and leaves the request
  in a clean state;
- the `type()` accessor returning the expected `uv::request_type` value.

## 6. Documentation

Add a section to the relevant user-facing doc under `docs/` (or create one).
Update `docs/design/architecture.md` if the directory layout changes.
