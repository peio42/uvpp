# Ownership and Lifetime

## Handles

libuv handles are address-stable. Once initialized, a handle's native address must not change while libuv may refer to it.

V2 handles disable copying and moving:

```cpp
handle(const handle&) = delete;
handle& operator=(const handle&) = delete;
handle(handle&&) = delete;
handle& operator=(handle&&) = delete;
```

This is stricter than typical C++ value types, but it reflects libuv's lifecycle accurately.

## Closing

`close()` is asynchronous in libuv. A destructor cannot safely call `uv_close()` and then destroy the object immediately.

V2 does not auto-close low-level handles.

Valid patterns:

- stack handle closed before `loop.run()` exits;
- heap handle deletes itself in the close callback;
- an application-defined owner schedules close and retains state through completion.

Library-provided asynchronous owners are tracked in
[proposal 002](../proposals/002-async-ownership.md).

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

  if (auto *addr = received.address()) {
    if (addr->sa_family == AF_INET) {
      uv::ipv4 peer{*reinterpret_cast<const sockaddr_in *>(addr)};
      (void)peer; // value copy; may be retained with payload
    } else if (addr->sa_family == AF_INET6) {
      uv::ipv6 peer{*reinterpret_cast<const sockaddr_in6 *>(addr)};
      (void)peer;
    }
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

Owning stream operations are tracked in
[proposal 005](../proposals/005-buffers-and-flow-control.md).

`fs::raw::request` is a special case because libuv uses `uv_fs_t` for every filesystem operation and requires `uv_fs_req_cleanup()` after completion. The raw API keeps that cleanup explicit. The callback must call `req.cleanup()` after consuming the result and before reusing or destroying the request. `req.scoped_cleanup()` is an opt-in stack guard for that call.

The public `uv::fs` API owns the raw request internally, cleans it automatically, and returns scalar or owned result values. Use `fs::raw` when the caller needs exact libuv control, request reuse, caller-owned buffers, static callbacks, or request-scoped directory iteration.

## Files and Directory Ownership

`file_descriptor` is a copyable integer wrapper, not a closing RAII owner.
A successful public `fs::open` still requires an explicit file close; ownership of
operation state does not imply automatic ownership of the opened file.

Incremental `opendir`/`readdir`/`closedir` are raw-only. Successful
`raw::opendir_result` must be consumed via `take_directory()`. That result and
`raw::directory` assert empty state on destruction; neither closes automatically.
`closedir` takes `directory&&` and releases its pointer after successful submission,
not before submission failure. Clean the last readdir request before submitting
close, and keep its entry array alive through cleanup. A higher-level directory
owner remains in [proposal 002](../proposals/002-async-ownership.md).

## Raw Filesystem Ranges

Raw filesystem results may expose range syntax without taking ownership of the
data they iterate.

`fs::raw::scandir_result` models libuv's `uv_fs_scandir_next()` protocol. Its
range is single-pass and consumes the underlying scandir cursor:

```cpp
uv::fs::raw::scandir(loop, req, path, 0,
  [](uv::fs::raw::request& req, uv::fs::raw::scandir_result result) {
    auto cleanup = req.scoped_cleanup();

    for (auto entry : result) {
      std::string name{entry.name()};
    }
  });
```

The entry names are borrowed. Copy a name before advancing the iterator or
calling `next()` again, as libuv frees the previous entry on advance; request
cleanup also invalidates it. Retaining the request alone does not retain an
already-consumed entry. See [libuv scandir implementation](https://github.com/libuv/libuv/blob/v1.x/src/uv-common.c).

`fs::raw::readdir_result::entries(buffer)` ranges over the entries libuv filled
in a caller-owned `directory_read_buffer` for that completion:

```cpp
for (auto entry : result.entries(buffer)) {
  std::string name{entry.name()};
}
```

The caller owns the entry array, but libuv allocates the entry names. Names become
invalid when the readdir request is cleaned, even if the entry array remains alive.
Copy names before cleanup; clean the completed request before reusing it or closing
the directory. See [libuv readdir](https://docs.libuv.org/en/v1.x/fs.html#c.uv_fs_readdir).
The range owns neither the array nor the names.

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

## Immediate Batch Metadata

Immediate low-level APIs that operate on multiple native items must not allocate
batch metadata internally. Their signatures should make every borrowed array
and address lifetime visible.

For UDP `send_many_now()`, the payload buffers, the arrays of buffer pointers,
the per-datagram buffer counts, and the destination address pointers are all
borrowed for the duration of the call. The wrapper must not store those pointers
after `uv_udp_try_send2()` returns, and it must not allocate replacement arrays
inside the call.

`udp_send_batch` is the higher-level value that explicitly owns reusable
metadata arrays. It still borrows payload bytes and destination addresses; the
type name and documentation do not imply copied payloads or owned addresses.

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

The typed API does not make libuv's `void*` type-safe. Retrieving a different type than the one stored is undefined user behavior and should be documented as such. The low-level layer does not add per-object type tracking. Proposed higher-level
ownership is tracked separately in the [ownership proposal](../proposals/002-async-ownership.md).

## Loop

The two current types separate storage ownership from borrowing:

- `loop` contains and initializes `uv_loop_t`; closing remains explicit.
- `loop_view` borrows a native loop and has no `close()` member. `default_loop()`
  returns a view, not an owning `loop`.

The public types are:

```cpp
class loop;
class loop_view;
```

An owning `loop` should not be confused with the process-wide default loop.

The low-level `loop` destructor does not call `uv_loop_close()`. Users must call `loop.close()` explicitly once all handles and requests associated with the loop are closed and the loop is no longer alive.

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

Higher-level owners and asynchronous cleanup are described in the
[ownership proposal](../proposals/002-async-ownership.md). They are not part of the
current low-level handle API.
