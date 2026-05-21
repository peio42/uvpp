# Streams

`uv::tcp`, `uv::pipe`, and `uv::tty` expose libuv stream operations with typed callback arguments.

## Listening And Accepting

```cpp
uv::loop loop;
uv::tcp server(loop);

server.bind(uv::ipv4{"127.0.0.1", 2345});

server.listen([&](uv::tcp& listener, uv::result status) {
  if (!status) {
    return;
  }

  auto* client = new uv::tcp(loop);
  listener.accept(*client);

  client->close([client](uv::tcp&) {
    delete client;
  });
});

loop.run();
loop.close();
```

The accepted client must remain alive until it is closed. Heap-owned clients are commonly deleted from their close callback.

## Reading

Reads use separate allocation and read callbacks, matching libuv's model.

```cpp
uv::owned_buffer storage{4096};

auto allocator = [&](uv::tcp&, std::size_t) {
  return storage.view();
};

client.read_start(allocator, [](uv::tcp& stream, uv::read_result read) {
  if (read.eof()) {
    stream.close();
    return;
  }

  if (!read) {
    stream.close();
    return;
  }

  auto bytes = read.bytes();
  (void)bytes;
});
```

Read result byte views are borrowed from the allocation callback storage.

## Writing

Writes use explicit request objects.

```cpp
uv::write_request request;
uv::owned_buffer payload{4};
std::memcpy(payload.data(), "pong", 4);

client.write(request, payload.view(),
  [](uv::write_request&, uv::result result) {
    if (!result) {
      return;
    }
  });
```

The request and payload storage must outlive the write completion callback.

## Shutdown

Use `uv::shutdown_request` for asynchronous stream shutdown.

```cpp
uv::shutdown_request request;

client.shutdown(request, [](uv::shutdown_request&, uv::result result) {
  if (!result) {
    return;
  }
});
```

## Pipe Names

`uv::pipe::bind()` and `uv::pipe::connect()` accept `std::string_view` names.
When building against libuv 1.46.0 or newer, uvpp uses the length-aware libuv
pipe APIs, so names may contain embedded null bytes for platform features such
as Linux abstract namespace sockets.

```cpp
#if UVPP_HAS_PIPE_BIND2 && UVPP_HAS_PIPE_CONNECT2
server.bind(name, uv::pipe_name_flag::no_truncate);
client.connect(request, name, uv::pipe_name_flag::no_truncate, callback);
#endif
```

`uv::pipe_name_flag::no_truncate` asks libuv to reject overlong pipe names
instead of silently truncating them.

## TTY Virtual Terminal State

When building against libuv 1.33.0 or newer, `uv::tty` exposes the process-wide
virtual terminal state helpers:

```cpp
#if UVPP_HAS_TTY_VTERM_STATE
uv::tty::set_vterm_state(uv::tty_vterm_state::supported);
auto state = uv::tty::vterm_state();
#endif
```

This feature is meaningful on Windows. On Unix, libuv reports
`UV_ENOTSUP` when reading the current state.
