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
Pass an optional `addrinfo` hints pointer to select its scalar `ai_flags`,
`ai_family`, `ai_socktype`, and `ai_protocol` fields. Those fields, the node,
and the service are copied before native submission; no caller input must remain
alive after the `resolve(...)` expression has created its awaiter.

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
  // Includes UV_ECANCELED when cancellation completed natively.
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

The underlying low-level `getaddrinfo_request` callback API remains separately
available for caller-owned request storage. It is not the high-level result
surface documented here.
