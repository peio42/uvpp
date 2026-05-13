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

## Borrowed Callback Results

Some handle callback result objects expose data borrowed from libuv or from caller-provided callback storage for the duration of the callback.

Examples:

- `fs_event_result::filename()` is a borrowed string view;
- `fs_poll_result::previous()` and `fs_poll_result::current()` are borrowed stat pointers;
- `read_result::bytes()`, `read_result::storage()`, and `read_result::raw_buffer()` refer to the buffer returned by the stream allocation callback;
- `udp_receive_result::bytes()`, `udp_receive_result::storage()`, and `udp_receive_result::raw_buffer()` refer to the buffer returned by the UDP allocation callback;
- `udp_receive_result::address()` is the source address pointer passed by libuv.

Copy those values inside the callback if they must survive it.

```cpp
stream.read_start(allocator, [](uv::tcp&, uv::read_result read) {
  if (!read || read.eof()) {
    return;
  }

  std::vector<std::byte> payload{read.bytes().begin(), read.bytes().end()};
});
```

```cpp
udp.receive_start(allocator, [](uv::udp&, uv::udp_receive_result received) {
  if (!received || received.empty_event()) {
    return;
  }

  std::vector<std::byte> payload{received.bytes().begin(), received.bytes().end()};

  sockaddr_storage peer{};
  if (auto *addr = received.address()) {
    std::memcpy(&peer, addr, sizeof(peer));
  }
});
```

This is deliberately different from `uv::fs::read_result`, which owns an `owned_buffer` in the public `uv::fs` API.

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

`fs::raw::request` is a special case because libuv uses `uv_fs_t` for every filesystem operation and requires `uv_fs_req_cleanup()` after completion. The raw API keeps that cleanup explicit. The callback must call `req.cleanup()` after consuming the result and before reusing or destroying the request. `req.scoped_cleanup()` is an opt-in stack guard for that call.

The public `uv::fs` API owns the raw request internally, cleans it automatically, and returns scalar or owned result values. Use `fs::raw` when the caller needs exact libuv control, request reuse, caller-owned buffers, static callbacks, or request-scoped directory iteration.

## Buffers

Separate owning buffers from non-owning views. The v2 taxonomy is:

- `buffer_view`: non-owning `uv_buf_t`-compatible view used by low-level libuv-facing APIs;
- `owned_buffer`: owns bytes and can explicitly produce a `buffer_view`;
- `std::span<std::byte>` for generic byte ranges.

Do not provide a single `buffer::cleanup()` style API in v2. Ownership should be represented by type, not by convention.

`owned_buffer` is a convenience owner, not an asynchronous operation owner. A `buffer_view` produced by `owned_buffer::view()` remains valid only while the `owned_buffer` is alive and has not been resized or destroyed.

```cpp
owned_buffer storage{4096};
auto view = storage.view();
```

For asynchronous writes and UDP sends, the memory referenced by the submitted `buffer_view` or byte span must outlive the operation completion callback. Low-level APIs do not copy buffer contents.

```cpp
owned_buffer payload{4};
std::memcpy(payload.data(), "ping", 4);
auto view = payload.view();

write_request req;
stream.write(req, view, [](write_request&, result status) {
  // payload must still be alive here
});
```

For read and UDP receive allocation callbacks, returning a `buffer_view` does not transfer ownership to uvpp. The allocator must ensure that the backing storage remains valid until the corresponding read/receive callback has run and released or reused it.

## Callback State

Runtime callbacks require storage. The owner should be clear:

- persistent handle callbacks are owned by the handle;
- one-shot request callbacks are owned by the request;
- high-level convenience operations own their internal state until completion.

Static callback mode does not own callback state.

## User Data

`uv_handle_t::data` and `uv_req_t::data` are non-owning application pointers. The `uv` API exposes them through `user_data<T>()`, but does not manage their lifetime.

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

`loop.close()` maps directly to `uv_loop_close()`: it succeeds only when libuv considers the loop closable, and throws `uv::error` on immediate failure. The destructor intentionally provides no hidden cleanup fallback because that would obscure leaked active handles.

`loop.walk(callback)` and `loop.handles()` expose handles through `handle_view`.
Those views are non-owning pointers to the underlying `uv_handle_t` objects.
They remain valid only while the native handle remains alive; do not keep them
past handle close/destruction unless the application independently guarantees
the native handle lifetime.

`handle_view::as<T>()` is a low-level recovery helper for handles known to have
been created by uvpp as `T`. Calling it with the wrong wrapper type, or with a
handle not created by uvpp, is undefined user behavior.

## Deallocation Rules

The low-level layer should never delete user objects implicitly unless the API clearly documents ownership transfer.

Prefer explicit close callbacks:

```cpp
client.close([](tcp& client) {
  delete &client;
});
```

A future high-level layer may provide safer owners, but it should be built on top of the low-level wrappers.
