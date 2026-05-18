# Streams, Reads, Writes, And Buffers

uvpp streams represent libuv stream objects: TCP, pipe, and TTY. They share the
`listen`, `accept`, `read_start`, `write`, and `shutdown` operations.

This page uses TCP in the examples, but the memory rules for buffers and requests
are the same for the other stream types.

## Minimal TCP Server

```cpp
#include <iostream>

#include <uvpp/uv.hpp>

int main() {
  uv::loop loop;
  uv::tcp server(loop);

  server.bind(uv::ipv4{"0.0.0.0", 2345});

  server.listen([&](uv::tcp& listener, uv::result status) {
    if (!status) {
      std::cerr << status.error_code().message() << '\n';
      return;
    }

    auto* client = new uv::tcp(loop);
    listener.accept(*client);
    listener.close();

    client->close([client](uv::tcp&) {
      delete client;
    });
  });

  loop.run();
  loop.close();
}
```

The server lives on the stack in `main`. Each client is allocated on the heap
because the number of connections is not known at compile time. The client is
destroyed from its close callback, not immediately after `accept()`.

This example accepts one connection, closes the server, then immediately closes
the client. It is intentionally simple: the goal is to identify the owners.

- `loop` owns the native loop.
- `server` owns the server handle.
- each `client` owns its native TCP handle.
- libuv borrows native addresses while handles are active.

## Reading From A Stream

libuv splits reading into two callbacks. The first callback provides a buffer
where libuv can place bytes. The second reports how many bytes were read.

```cpp
uv::owned_buffer input{4096};

auto allocator = [&](uv::tcp&, std::size_t suggested_size) {
  if (input.size() < suggested_size) {
    input.resize(suggested_size);
  }

  return input.view();
};

client.read_start(allocator, [](uv::tcp& stream, uv::read_result read) {
  if (read.eof()) {
    stream.close();
    return;
  }

  if (!read.ok()) {
    stream.close();
    return;
  }

  std::span<const std::byte> bytes = read.bytes();
  (void)bytes;
});
```

`allocator` returns a `buffer_view`. That view borrows memory from `input`. It
does not transfer ownership to uvpp or libuv. `input` must therefore stay alive
while the read can use it.

In this example, a single buffer is reused. That strategy can be fine for a
connection that processes data inside the callback before returning control. It
is not fine if the bytes are used later by another asynchronous operation without
being copied.

## Echo: Read And Write The Same Bytes

An echo server must keep read bytes alive until the write completes. The simplest
low-level approach is to create one small owner per write.

```cpp
struct echo_write {
  uv::write_request request;
  uv::owned_buffer payload;
};

client.read_start(allocator, [](uv::tcp& stream, uv::read_result read) {
  if (read.eof()) {
    stream.close();
    return;
  }

  if (!read.ok()) {
    stream.close();
    return;
  }

  auto* write = new echo_write{
    uv::write_request{},
    uv::owned_buffer{read.bytes()}
  };

  stream.write(write->request, write->payload.view(),
    [write](uv::write_request&, uv::result status) {
      if (!status) {
        std::cerr << status.error_code().message() << '\n';
      }

      delete write;
    });
});
```

`read.bytes()` is a callback-scoped view. The `owned_buffer` constructor copies
those bytes into `write->payload`. The write then borrows `write->payload`
through `view()`. The write callback deletes the owner when libuv no longer
needs the request or the buffer.

Without that copy, or without another explicit owner, the write could point to
memory that has already been reused by the next read.

## Accepting And Reading Multiple Clients

For a real connection, it is useful to group the client handle, read buffer, and
in-flight operations in a session object.

```cpp
struct session {
  uv::tcp client;
  uv::owned_buffer input{4096};

  explicit session(uv::loop& loop)
    : client(loop) {}
};

server.listen([&](uv::tcp& listener, uv::result status) {
  if (!status) {
    std::cerr << status.error_code().message() << '\n';
    return;
  }

  auto* s = new session(loop);
  listener.accept(s->client);

  auto allocator = [s](uv::tcp&, std::size_t suggested_size) {
    if (s->input.size() < suggested_size) {
      s->input.resize(suggested_size);
    }
    return s->input.view();
  };

  s->client.read_start(allocator, [s](uv::tcp& stream, uv::read_result read) {
    if (read.eof() || !read.ok()) {
      stream.close([s](uv::tcp&) {
        delete s;
      });
      return;
    }

    // Process read.bytes() here, or copy if the bytes must survive.
  });
});
```

The `s` pointer is captured by callbacks because the session owns the client
handle and the buffer. It is destroyed only in the client's close callback.

A more advanced session would also store pending writes, protocol counters,
authentication state, and so on. The rule is the same: every asynchronous
operation needs a visible owner.

## Immediate Writes And Asynchronous Writes

`write()` submits an asynchronous write and uses a `uv::write_request`.

```cpp
stream.write(request, payload.view(), callback);
```

`write_now()` attempts an immediate write without a request.

```cpp
auto written = stream.write_now(payload.view());

if (written.would_block()) {
  // Submit an asynchronous write or retry later.
}
```

`write_now()` is useful when the application knows how to handle "not now". It
does not replace `write()` for the general case, because a non-blocking socket
can temporarily refuse more bytes.

## Close And Shutdown

`shutdown()` cleanly shuts down the write side of a stream and uses a
`uv::shutdown_request`.

```cpp
uv::shutdown_request shutdown;

stream.shutdown(shutdown, [](uv::shutdown_request&, uv::result status) {
  if (!status) {
    return;
  }
});
```

As with `write_request`, the request must outlive the callback. `shutdown()` does
not destroy the handle. To release the handle, use `close()` later when your
protocol requires it.

```cpp
stream.shutdown(shutdown, [&](uv::shutdown_request&, uv::result) {
  stream.close();
});
```

This form captures `stream` by reference. It is correct only if the handle stays
alive until the shutdown callback and until close completion.

