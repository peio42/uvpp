# Errors

uvpp separates immediate submission failures from asynchronous completion failures.

Immediate failures happen before libuv accepts an operation. The primary low-level API reports them by throwing `uv::error`.

```cpp
try {
  uv::tcp server(loop);
  server.bind(uv::ipv4{"127.0.0.1", 2345});
} catch (const uv::error& error) {
  std::cerr << error.what() << '\n';
}
```

Asynchronous failures happen later, in a libuv completion callback. They are delivered through `uv::result` or an operation-specific result object.

```cpp
uv::connect_request request;

client.connect(request, uv::ipv4{"127.0.0.1", 2345},
  [](uv::connect_request&, uv::result result) {
    if (!result) {
      auto ec = result.error_code();
      (void)ec;
      return;
    }
  });
```

For cancellable requests, `uv::result::canceled()` is the named branch for
`UV_ECANCELED`.

Filesystem operations use typed results such as `uv::fs::open_result`, `uv::fs::read_result`, and `uv::fs::status_result`.

```cpp
uv::fs::stat(loop, "file.txt", [](uv::fs::stat_result result) {
  if (!result) {
    auto ec = result.error_code();
    (void)ec;
    return;
  }

  auto size = result.native().st_size;
  (void)size;
});
```

EOF on streams is a normal event, not an exception.

```cpp
stream.read_start(allocator, [](uv::tcp& stream, uv::read_result read) {
  if (read.eof()) {
    stream.close();
    return;
  }

  if (!read) {
    return;
  }
});
```
