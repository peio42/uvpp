# DNS

`resolve` is the experimental high-level coroutine wrapper for libuv
`uv_getaddrinfo`. It has no persistent DNS owner: the request belongs to the
awaiting coroutine frame and is submitted on that task's loop.

```cpp
#include <uvpp/net/dns.hpp>

uv::co::task<void> connect_later() {
  auto addresses = co_await uv::resolve("localhost", "443");
  for (const uv::address_info& address : addresses) {
    // address is a value; it remains valid after this coroutine continues.
  }
}
```

`resolved_addresses` is `std::vector<address_info>`. Each `address_info`
contains a copied socket address, the relevant address-family/socket/protocol
fields, and a copied canonical name. It never borrows libuv's `addrinfo` list.

`resolve` accepts a value configuration rather than a native `addrinfo` hint
pointer:

```cpp
auto addresses = co_await uv::resolve("localhost", "443", uv::resolve_options{
  .family = AF_INET,
  .socket_type = SOCK_STREAM,
});
```

`resolve_options` defaults to `flags = 0`, `family = AF_UNSPEC`,
`socket_type = 0`, and `protocol = 0`. It exposes precisely the scalar values
that libuv uses as hints. The configuration, node, and service are copied before
native submission; no caller input must remain alive after the `resolve(...)`
expression has created its awaiter. The same configuration overloads are
available as `uv::ops::resolve`.

## Error surfaces

The normal facade throws `uv::error` at `co_await` for both immediate libuv
submission errors and later DNS completion errors. The corresponding `uv::ops`
facade returns the same operational failures as `resolve_result`:

```cpp
auto result = co_await uv::ops::resolve("localhost", "443");
if (result) {
  for (const uv::address_info& address : result.value()) {
    // use address
  }
} else {
  uv::error_code error = result.error();
  // Includes the native DNS cancellation or completion error.
}
```

`resolve_result` is `uv::result<resolved_addresses>`. Result selection only
changes failure delivery; both facades use the same request, inputs, native
submission, cancellation and completion path. Copying strings at awaiter
construction and materializing the returned vector can throw normal C++ errors
(for example `std::bad_alloc`) even for `uv::ops`.

## Loop, cancellation, and lifetime

`resolve` takes no loop parameter. A cold task gets its loop at `spawn`, and the
request always submits on precisely that loop. It therefore cannot be
accidentally submitted on a loop different from its awaiting task. As with other
coroutine operations, use it only from a spawned task.

A pre-existing task stop completes the operation as `UV_ECANCELED` without
submitting a request. Once submitted, a stop requests `uv_cancel`. Libuv may
decline cancellation because the worker has already completed; normal DNS
completion then wins. In either case the coroutine frame, native request, copied
inputs, and libuv address list remain alive until the terminal callback has
released its cancellation registration and resumed the coroutine. A stop request
is never permission to destroy that state early.

## Raw DNS requests

`uv::raw::getaddrinfo_request` and `uv::raw::getnameinfo_request` contain the
native request. The caller keeps each request address stable and alive until
its completion callback, even after requesting cancellation. `pending()` is
cleared before callback delivery, so the request can be reused from the callback.
An overlapping submission returns `UV_EBUSY`; `cancel()` returns a status and
is only a cancellation request. Application `data` remains available.

`uv::raw::getaddrinfo` and `uv::raw::getnameinfo` take caller-owned requests,
a loop and runtime callbacks; `getaddrinfo_static` and `getnameinfo_static` take
compile-time callbacks. Submission returns `uv::status`, independently of the
completion result. Callback construction may allocate and throw before entry;
native failures remain statuses. The raw address lookup takes NUL-terminated
`const char*` node and service arguments (either may be null, but not both), and
an optional native `addrinfo` hints pointer. Libuv copies inputs during submission.
The reverse lookup accepts a native socket address pointer, also copied by libuv.

`raw::getaddrinfo_result` uniquely owns the returned native `addrinfo` list and
frees it with `uv_freeaddrinfo` on destruction or `reset()`. It can be moved out
of the callback; its iterators and views borrow from the list. `to_vector()`
creates owned C++ values and can allocate. The separate `raw::getnameinfo_result`
contains borrowed host and service strings stored in the native request. Copy
those strings before reusing or destroying that request. Both results retain the
native completion status, including cancellation and DNS failures. High-level
`resolve` keeps its own request and frees the native list transparently after
producing owned addresses.

## IPv6 interface identifiers

The existing `<uvpp/net/interface.hpp>` helpers expose libuv's platform behavior.
`interface_name(index)` looks up an interface name. `interface_identifier(index)`
returns a scope identifier: Windows formats the numeric index (including zero)
without validating it, while Unix looks up the interface name and rejects zero.
Use an actual interface index when building a scoped IPv6 address.
