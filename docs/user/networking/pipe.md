# Pipe

Include `<uvpp/net/pipe_connection.hpp>` and `<uvpp/net/pipe_listener.hpp>`.
These experimental move-only owners use stable native storage for local streams.
Native endpoint naming and availability depend on the platform.

`co_await uv::pipe_connection::connect(name)` creates an outgoing connection on
the awaiting task's loop. `uv::pipe_listener{loop, name}` binds and listens;
`co_await listener.accept()` returns a separate `pipe_connection` owner.
Listener construction can throw. As with [TCP](tcp.md), accept is one-shot and
has no hidden connection queue when no waiter is armed.

Connections provide borrowing `write(std::string_view)` and
`read_some(std::span<std::byte>)`, with stream byte count and EOF results.
One inbound and one outbound operation may overlap; another operation in the
same direction fails with `UV_EBUSY`. Submitted writes retain their payload
through completion even after a stop request. Reads and accepts support
cooperative cancellation.

## IPC handle passing

Pass `ipc = true` when creating the IPC connection/listener. This slice supports
exporting TCP connections over an IPC pipe:

```cpp
// Sending task: ipc_pipe and tcp_connection belong to its loop.
co_await ipc_pipe.write_with_handle("control", tcp_connection);
```

On the receiving endpoint, using caller-owned scratch bytes:

```cpp
auto received = co_await ipc_pipe.receive_handle(scratch);
while (received.tcp_count() != 0) {
  auto connection = received.take_tcp();
  co_await connection.close();
}
```

Export borrows and pins the source TCP owner through
write completion; it does not transfer that C++ owner. Receiving constructs fresh
stable TCP owners. `bytes_transferred()` reports accumulated control bytes and
`tcp_count()` reports queued received owners.

Use a dedicated control pipe: bytes and pending handles are not framed or paired
by this API. Scratch exhaustion before a handle is visible fails with `UV_ENOBUFS`.
Unsupported pending handle types fail closed. `receive_handle` shares the read slot
with `read_some`; `write_with_handle` shares the write slot with `write`.

## Cleanup

Connection close rejects active I/O, and listener close can quiesce one active
accept. Await `close()` or `uv::ops::close(owner)` through native completion.
Connections and listeners can also be adopted by a resource scope. See
[ownership and lifetime](../ownership-and-lifetime.md) for exceptional cleanup
and [buffers](../buffers.md) for byte lifetimes.
