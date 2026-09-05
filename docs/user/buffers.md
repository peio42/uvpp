# Buffers

uvpp separates owned storage from borrowed views.

- `uv::buffer_view` is a non-owning `uv_buf_t`-compatible view.
- `uv::owned_buffer` owns bytes and can produce a `buffer_view`.
- `std::span<std::byte>` and `std::span<const std::byte>` are used for generic byte ranges.

```cpp
uv::owned_buffer storage{4096};
uv::buffer_view view = storage.view();
```

Creating a `buffer_view` does not transfer ownership. The referenced memory must remain alive while libuv may use it.

The view preserves every length representable by the native `uv_buf_t::len` field. On a platform where that field is narrower than `std::size_t`, constructing a larger view throws `std::length_error`; uvpp never silently truncates the length. Likewise, operations reject an oversized number of buffers before submission. The non-throwing `*_now()` operations report that condition as `UV_EINVAL`.

## Writes

Low-level async writes and UDP sends do not copy submitted buffers.

```cpp
uv::owned_buffer payload{4};
std::memcpy(payload.data(), "ping", 4);

uv::write_request request;
stream.write(request, payload.view(),
  [](uv::write_request&, uv::result result) {
    if (!result) {
      return;
    }
  });
```

`payload` and `request` must both remain alive until the write callback runs.

## Reads

Stream and UDP receive allocators return borrowed `buffer_view` values. The allocator controls the backing storage lifetime.

```cpp
uv::owned_buffer storage{4096};

auto allocator = [&](uv::tcp&, std::size_t) {
  return storage.view();
};

stream.read_start(allocator, [](uv::tcp&, uv::read_result read) {
  if (!read || read.eof()) {
    return;
  }

  auto bytes = read.bytes();
  (void)bytes;
});
```

`read.bytes()`, `read.storage()`, and `read.raw_buffer()` are callback-scoped views unless the application explicitly owns the backing storage for longer.

## Filesystem Buffers

The public `uv::fs` layer owns operation state internally. For example, `uv::fs::write` copies submitted bytes into operation-owned storage, and `uv::fs::read_result` owns its read buffer.

Use `uv::fs::raw` when exact libuv buffer ownership and request reuse matter.
