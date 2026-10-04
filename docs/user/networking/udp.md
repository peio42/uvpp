# UDP

Include `<uvpp/net/udp_socket.hpp>`. The experimental `uv::udp_socket` is a
move-only owner with stable native storage. Construct it with its loop and an
IPv4 or IPv6 local address; construction binds the socket and may throw.

`uv::ops::make_udp_socket(loop, address) -> result<udp_socket>` provides
synchronous explicit-result construction for IPv4 and IPv6. Move the successful
value into a socket owner. Native init/bind failures become results; C++ allocation
failures still throw. Failure after native init schedules close before error
delivery; keep driving the loop for cleanup as described in [errors](../errors.md).

```cpp
#include <uvpp/net/udp_socket.hpp>

uv::co::task<void> send_datagram(uv::loop& loop) {
  uv::udp_socket socket{loop, uv::ipv4{"127.0.0.1", 0}};
  co_await socket.send_to("hello", uv::ipv4{"127.0.0.1", 8080});
  co_await socket.close();
}
```

Spawn this task on the same `loop`. `send_to` borrows its payload until actual
send completion. Byte-span overloads support IPv4 and IPv6 destinations; the
string-view convenience overload currently takes IPv4.

`co_await socket.recv_from(buffer)` borrows a `std::span<std::byte>` and returns
one datagram result with `size()`, `partial()`, and `peer_address()`. The peer
address is copied; payload bytes remain in caller storage. `partial()` reports
native truncation. `co_await uv::ops::recv_from(socket, buffer)` returns
`result<udp_socket::recv_from_result>` with the same payload, while
`co_await uv::ops::send_to(socket, bytes, address)` returns `status`. These forms
mirror the member overloads and also accept borrowed socket views. Native
submission/completion errors, busy, closed, and completed cancellation outcomes
become results. Buffer-length validation can still throw during preparation;
wrong-loop use and expired views remain C++ misuse exceptions.

An empty datagram completes with size zero; an empty native
notification without a datagram does not complete the operation.

One send and one receive may overlap. A second operation in the same direction
fails with `UV_EBUSY`. Receive completion stops the native receiver and releases
its slots before resuming the task. Cooperative stop cancels receive, while an
already submitted send retains the payload borrow until actual completion.

Close rejects active I/O. After settling it, await `socket.close()` or
`uv::ops::close(socket)`, or adopt the owner into a resource scope. See
[ownership](../ownership-and-lifetime.md), [buffers](../buffers.md), and
[errors](../errors.md). There is no bounded receive queue or lossless backpressure
guarantee in this slice.

`native()` and `native_handle()` borrow libuv storage. Its `data` field belongs
to uvpp; callers must not overwrite it, replace uvpp-owned callbacks, or
independently close the handle. Moving the owner preserves this storage and its
callback state. Raw wrappers keep `data` available to application code.
