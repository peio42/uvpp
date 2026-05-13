# Ownership And Lifetime

libuv handles are address-stable. uvpp handles are therefore not copyable or movable after initialization.

```cpp
uv::tcp client(loop);
```

Keep a handle object alive while libuv can still reference its native handle. Destroying a wrapper while callbacks can still run is a user lifetime error.

## Closing Handles

`close()` maps to asynchronous `uv_close()`. It schedules the close and returns before libuv has finished with the native handle.

For stack-owned handles, close them before leaving the scope and run the loop until close callbacks have completed.

```cpp
uv::timer timer(loop);

timer.start(100ms, [&](uv::timer& self) {
  self.close();
});

loop.run();
loop.close();
```

For heap-owned handles, delete the object from the close callback.

```cpp
auto* client = new uv::tcp(loop);

client->close([client](uv::tcp&) {
  delete client;
});
```

## Requests

Requests must outlive the asynchronous operation that uses them.

```cpp
uv::write_request request;

stream.write(request, view, [](uv::write_request&, uv::result result) {
  if (!result) {
    return;
  }
});
```

Do not destroy or reuse a request until its completion callback has run.

## Borrowed Callback Data

Some callback result objects expose borrowed data. Copy it inside the callback if it must survive the callback.

```cpp
stream.read_start(allocator, [](uv::tcp&, uv::read_result read) {
  if (!read || read.eof()) {
    return;
  }

  std::vector<std::byte> payload{read.bytes().begin(), read.bytes().end()};
});
```

`uv::fs` public results are different: they own or copy their returned values so those values can safely outlive the callback.

## User Data

libuv `data` fields belong to the application. uvpp exposes them as non-owning typed pointer helpers.

```cpp
struct session {};

session state;
client.user_data(state);

auto* current = client.user_data<session>();
client.clear_user_data();
```

The typed getter is a cast convenience over `void*`; it does not enforce type safety or ownership.
