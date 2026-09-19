# TCP

Include `<uvpp/net/tcp_connection.hpp>` and `<uvpp/net/tcp_listener.hpp>`.
The implemented v3 owners are experimental, move-only, and affine to one loop.
Moving an owner preserves its native address.

## Connecting and stream I/O

```cpp
#include <uvpp/net/tcp_connection.hpp>

uv::co::task<void> send_greeting() {
  auto connection = co_await uv::tcp_connection::connect(
      uv::ipv4{"127.0.0.1", 8080});
  co_await connection.write("hello");
  co_await connection.close();
}
```

Spawn the task on a loop as shown in [getting started](../getting-started.md).
`connect()` also accepts IPv6. It uses the awaiting task's loop and closes failed
provisional connections before delivering an error.

`write(std::string_view)` borrows its bytes through completion.
`read_some(std::span<std::byte>)` borrows mutable storage and returns `count()`
and `eof()`; operational failures throw at the await. One read and one write may
overlap. A second operation in either direction fails with `UV_EBUSY`.
Read completion releases the native read slots before resuming the task.
See [buffers](../buffers.md) and [errors](../errors.md).

## Accepting

Construct `uv::tcp_listener{loop, address}` to bind and start listening.
`co_await listener.accept()` returns a distinct `tcp_connection` owner; only one
accept may be pending. Constructor setup can throw before any await.

This is a one-shot accept API. Keep an accept waiter armed in server code: the
prototype ignores native notifications when no waiter is armed, and has no hidden
accepted-connection queue, `serve(handler)`, or overload policy. Do not treat a
sequential accept/handle loop as a complete production server abstraction.

## Stop and cleanup

Cooperative stop cancels a pending read or accept with `UV_ECANCELED` after its
native claims are released. Submitted connect/write work is not physically
cancelled; keep the owner and borrowed bytes alive through completion.

`co_await connection.close()` waits for native close and rejects active I/O.
Listener close can quiesce its pending accept but does not close accepted peers.
Both owners support `uv::ops::close` and adoption into a resource scope.
Follow [ownership and lifetime](../ownership-and-lifetime.md) on success and
exception paths; owner destruction may leave native cleanup for the loop to drive.
