# Buffers

V3 network writes and sends borrow their payload by default. A task frame or an
operation owner does not keep external data alive. Keep submitted bytes alive,
address-stable, and unmodified until the await completes, including after a stop
request. Do not construct a deferred awaiter from a temporary payload that will
be gone when the await begins.

```cpp
// Inside a task; socket is a TCP or pipe connection.
std::string payload = "hello";
co_await socket.write(payload);
// payload may now be modified or destroyed.
```

`read_some(std::span<std::byte>)` and UDP `recv_from(std::span<std::byte>)` borrow
mutable caller storage until completion. Only use the returned byte count; a
stream result additionally reports `eof()`, and a UDP result reports `partial()`
and a copied peer address. The result does not own the caller's bytes.

The callback filesystem `read` and `write` operations follow the same rule:
their supplied spans remain borrowed through the completion callback. Use
`uv::fs::write_copy` or `uv::fs::read_owned` when the operation must own its
source or destination storage.

```cpp
std::array<std::byte, 4096> storage;
auto read = co_await socket.read_some(storage);
if (!read.eof()) {
  auto received = std::span{storage}.first(read.count());
  // Consume received before reusing storage for another read.
}
```

Include `<string>`, `<array>`, and `<span>` for these fragments. Stream reads are
one-shot: terminal delivery stops the native reader and releases its slots before
resuming the task. A new read may start immediately afterward.

Shared storage vocabulary also includes `uv::owned_buffer` and its explicit
`.view()` producing a borrowed `uv::buffer_view`, declared in
`<uvpp/net/buffer.hpp>`. A view never extends storage lifetime.

`write_copy`, owning read adapters, `read_exactly`, buffer pools, and bounded
queues remain proposed. See [proposal 005](../proposals/005-buffers-and-flow-control.md).
