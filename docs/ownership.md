# Ownership and Lifetime

## Handles

libuv handles are address-stable. Once initialized, a handle's native address must not change while libuv may refer to it.

Therefore v2 handles should be:

```cpp
handle(const handle&) = delete;
handle& operator=(const handle&) = delete;
handle(handle&&) = delete;
handle& operator=(handle&&) = delete;
```

This is stricter than typical C++ value types, but it reflects libuv's lifecycle accurately.

## Closing

`close()` is asynchronous in libuv. A destructor cannot safely call `uv_close()` and then destroy the object immediately.

v2 should avoid pretending that handle destruction automatically completes close.

Valid patterns:

- stack handle closed before `loop.run()` exits;
- heap handle deletes itself in the close callback;
- owning wrapper schedules close and keeps state alive until the close callback;
- higher-level RAII owner type separate from the low-level handle.

The low-level handle destructor does not call `uv_close()`. Destroying a handle while libuv can still reference it is a user lifetime error.

## Requests

Requests have operation-specific lifetimes. The request must outlive the asynchronous operation that uses it.

Low-level API:

```cpp
write_request req;
tcp.write(req, buffers, callback);
```

Higher-level API:

```cpp
tcp.async_write(buffers, callback);
```

The higher-level API may allocate operation state internally. That cost must be visible in the API name or documentation.

## Buffers

Separate owning buffers from non-owning views. The v2 taxonomy is:

- `buffer_view`: non-owning `uv_buf_t`-compatible view used by low-level libuv-facing APIs;
- `owned_buffer`: owns memory and can produce `buffer_view`;
- `std::span<std::byte>` for generic byte ranges.

Do not provide a single `buffer::cleanup()` style API in v2. Ownership should be represented by type, not by convention.

## Callback State

Runtime callbacks require storage. The owner should be clear:

- persistent handle callbacks are owned by the handle;
- one-shot request callbacks are owned by the request;
- high-level convenience operations own their internal state until completion.

Static callback mode does not own callback state.

## User Data

`uv_handle_t::data` and `uv_req_t::data` are non-owning application pointers. uvpp exposes them through `user_data<T>()`, but does not manage their lifetime.

Valid low-level pattern:

```cpp
session state;
client.user_data(state);

client.read_start(allocator, [](tcp& client, read_result read) {
  auto* state = client.user_data<session>();
});
```

The stored object must outlive every callback that may read it. Clearing the field only removes the pointer; it does not destroy the pointed object.

The typed API does not make libuv's `void*` type-safe. Retrieving a different type than the one stored is undefined user behavior and should be documented as such. A future high-level owner may add stronger typing, but the low-level layer should not add per-object storage for it.

## Loop

`loop` can be:

- owning: contains `uv_loop_t raw_` and initializes/closes it;
- non-owning view: references an external `uv_loop_t`, such as `uv_default_loop()`.

The two concepts should be explicit if the implementation needs both:

```cpp
class loop;
class loop_view;
```

An owning `loop` should not be confused with the process-wide default loop.

The initial low-level `loop` destructor does not call `uv_loop_close()`. Users must call `loop.close()` explicitly once all handles and requests associated with the loop are closed and the loop is no longer alive.

`loop.close()` maps directly to `uv_loop_close()`: it succeeds only when libuv considers the loop closable, and throws `uvpp::error` on immediate failure. The destructor intentionally provides no hidden cleanup fallback because that would obscure leaked active handles.

## Deallocation Rules

The low-level layer should never delete user objects implicitly unless the API clearly documents ownership transfer.

Prefer explicit close callbacks:

```cpp
client.close([](tcp& client) {
  delete &client;
});
```

A future high-level layer may provide safer owners, but it should be built on top of the low-level wrappers.
