# UDP

`uv::udp` exposes libuv datagram sockets with explicit buffer and request
lifetime.

## Initialization

The default constructor uses `uv_udp_init()` and creates the native socket
lazily.

```cpp
uv::loop loop;
uv::udp socket(loop);
```

Use `uv::udp_socket_family` when the socket family should be selected at
initialization time.

```cpp
uv::udp ipv4(loop, uv::udp_socket_family::ipv4);
uv::udp ipv6(loop, uv::udp_socket_family::ipv6);
```

`uv::udp_init_flag::recvmmsg` requests libuv's recvmmsg receive path where the
platform supports it. This helper is available when building against libuv
1.37.0 or newer.

```cpp
uv::udp socket(loop, uv::udp_socket_family::ipv4, uv::udp_init_flag::recvmmsg);

bool enabled = socket.using_recvmmsg(); // libuv 1.39.0 or newer
```

When recvmmsg is active, libuv expects UDP receive buffers to be multiples of
64 KiB.

## Binding

Bind flags have typed helpers for ordinary use.

```cpp
socket.bind(uv::ipv4{"0.0.0.0", 1234}, uv::udp_bind_flag::reuse_address);
```

Flags can be combined with `|`.

```cpp
socket.bind(uv::ipv6{"::", 1234},
            uv::udp_bind_flag::ipv6_only | uv::udp_bind_flag::reuse_address);
```

Raw `unsigned int` flag overloads remain available for native interop.

## Receiving

UDP receive uses the same allocation model as stream reads. The allocation
callback returns a borrowed `buffer_view`; `udp_receive_result` exposes views
into that storage.

```cpp
uv::owned_buffer storage{4096};

socket.receive_start(
  [&](uv::udp&, std::size_t) {
    return storage.view();
  },
  [](uv::udp&, uv::udp_receive_result received) {
    if (received.empty_event()) {
      return;
    }

    if (!received) {
      return;
    }

    auto bytes = received.bytes();
    auto* peer = received.address();
    (void)bytes;
    (void)peer;
  });
```

`received.address()` is borrowed from libuv and is valid only during the
callback.

## Sending

Asynchronous sends use an explicit `uv::udp_send_request`.

```cpp
uv::udp_send_request request;
uv::owned_buffer payload{4};
std::memcpy(payload.data(), "ping", 4);

socket.send(request, payload.view(), uv::ipv4{"127.0.0.1", 1234},
  [](uv::udp_send_request&, uv::result status) {
    if (!status) {
      return;
    }
  });
```

The request and payload storage must outlive the send completion callback.

## Immediate Sends

`send_now()` maps to libuv's immediate UDP send operation. It does not take a
request and it does not queue work when the socket cannot send immediately.

```cpp
auto result = socket.send_now(payload.view(), uv::ipv4{"127.0.0.1", 1234});

if (result.would_block()) {
  return;
}

if (result.has_error()) {
  auto error = result.error_code();
  (void)error;
  return;
}

auto bytes = result.bytes_sent();
```

`send_many_now()` maps to `uv_udp_try_send2()` and attempts to send multiple
datagrams immediately. It is available when building against libuv 1.50.0 or
newer. The result reports datagram count rather than byte count.

```cpp
std::array first_buffers{first.view()};
std::array second_buffers{second.view()};
std::array raw_first{*first_buffers[0].native()};
std::array raw_second{*second_buffers[0].native()};

std::array buffer_arrays{raw_first.data(), raw_second.data()};
std::array buffer_counts{1u, 1u};
std::array addresses{destination.native_sockaddr(), destination.native_sockaddr()};

auto result = socket.send_many_now(
  uv::udp_send_many_view{buffer_arrays, buffer_counts, addresses});

if (result.ok()) {
  auto sent = result.datagrams_sent();
  (void)sent;
}
```

`uv::udp_send_many_view` is a raw borrowed view over the parallel arrays expected
by libuv. It does not own buffers, address objects, or batch metadata; keep all
arrays alive for the duration of the immediate call.

For ordinary C++ call sites, use `uv::udp_send_batch`. The batch owns the
metadata arrays needed by libuv while still borrowing payload bytes and
destination addresses.

```cpp
std::array first{'o', 'n', 'e', '!'};
std::array second{'t', 'w', 'o', '!'};

auto batch = uv::udp_send_batch{};
batch.reserve(2, 2)
     .add(std::as_bytes(std::span{first}), destination)
     .add(std::as_bytes(std::span{second}), destination);

auto result = socket.send_many_now(batch);
```

`udp_send_batch::add()` accepts a single `buffer_view`, a span of
`buffer_view`s, or a `std::span<const std::byte>`. Passing no destination sends
on a connected UDP socket. Payload storage and address objects must remain alive
until `send_many_now()` returns. Do not call `add()` while the batch is being
consumed by `send_many_now()`. The batch overload of `send_many_now()` takes a
mutable batch because libuv receives mutable metadata arrays for the immediate
call.

If allocation fails during `add()` pre-reservation, the batch's datagrams and
payload metadata remain unchanged, but previously obtained `udp_send_batch_view`
values may be invalidated by successful reallocations before the failure.
Operations that successfully pre-reserve will not throw during element
insertion.

## Multicast

Use `set_membership()` for multicast group membership and
`set_source_membership()` for source-specific multicast.

```cpp
socket.set_membership("239.255.0.1", uv::membership::join);

socket.set_source_membership("239.255.0.1", "192.0.2.10",
                             uv::membership::join);
```
