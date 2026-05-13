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
