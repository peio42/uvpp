# Raw TCP handles

`uv::raw::tcp` is an address-stable, caller-owned TCP handle on the shared
`uv::loop`. Include `<uvpp/handles/tcp.hpp>` or `<uvpp/uv.hpp>`. It is neither
copyable nor movable; native `data` belongs to the application. There is no
`uv::tcp` compatibility alias. Common addresses and constants remain in `uv`:
`ipv4`, `ipv6`, `socket_address`, `tcp_socket_family`, and `tcp_bind_flag`.

This first network migration covers initialization, socket adoption, bind,
address queries, existing TCP options, and ordinary/reset close. The inherited
stream and request submission APIs retain their transitional contracts below.
The high-level `tcp_connection` / `tcp_listener` owners keep their independent
stable storage and existing coroutine/`uv::ops` contracts.

## Initialization and errors

```cpp
uv::loop loop;
uv::raw::tcp socket;
auto initialized = socket.init(loop);
if (initialized) {
  auto bound = socket.bind(uv::ipv4{"127.0.0.1", 0});
  if (bound) {
    auto local = socket.sockname(); // result<socket_address>, owning fixed-size value
    if (local) {
      auto port = local.value().port();
      (void)port;
    }
  }
  socket.close();
}
loop.run(); // Retain socket through native close completion.
loop.close();
```

`tcp() noexcept` leaves uninitialized storage. `init(loop&)` and `init(loop_view)`
are `noexcept` and return `status`. Loop-taking constructors unwrap that same
status with `.throw_if_error()`. A valid initialized loop is required. Under
`UVPP_HAS_TCP_INIT_EX`, loop and loop-view constructors/init overloads also accept
`tcp_socket_family::{ipv4,ipv6}`; native family init creates a socket immediately,
while default native init can defer socket creation until bind/open/connect.

`initialized()` tests native `loop != nullptr`, with no added state field. Native
addresses and application `data` survive init. A failed native init leaves no
registered handle, restores the null sentinel, preserves `data`, and permits
retry/destruction without close. Libuv TCP init already removes its failed native
registration and closes any newly created socket; uvpp clears the stale native
fields without unlinking a second time. Loop-owned preparation costs remain with
the loop. No nested loop or asynchronous failed-constructor cleanup is introduced.

A second successful-init attempt, including with another loop or during/after
close, returns `UV_EALREADY` without changing the socket, loop, or callbacks.
After successful init, close and retained storage through completion are required
even if bind/open subsequently fail. Storage reuse after success is unsupported.

Before init, migrated TCP operations and native queries return `UV_EBADF`;
`active`, `closing`, and `has_ref` return false; ref/unref/close variants are inert.
A pre-init close callback is neither retained nor invoked. Native access exposes
storage, not permission to invoke libuv on an uninitialized object; use borrowed
views only after successful init. All operations are loop-thread only.

## TCP operations and result payloads

| Operation | Result |
| --- | --- |
| `open(uv_os_sock_t)` | `status`; successful adoption transfers socket close responsibility to the handle |
| `bind(ipv4/ipv6, unsigned flags = 0)` | `status` |
| `bind(ipv4/ipv6, tcp_bind_flag)` | `status`; flags also compose with `operator|` |
| `sockname() const`, `peername() const` | `result<socket_address>`; retain address family, port and native sockaddr payload |
| `no_delay(bool)` | `status` |
| `keep_alive(chrono::seconds delay)` | `status`; enables keepalive with positive delay representable by native `unsigned int` seconds |
| `disable_keep_alive()` | `status`; disables keepalive |
| `simultaneous_accepts(bool)` | `status`; native platform semantics apply |
| `fileno() const` | `result<uv_os_fd_t>` |
| Buffer-size getters / setters | `result<int>` / `status` |

These operations are `noexcept` after argument preparation. Negative, zero, or
unrepresentable keepalive delays report `UV_EINVAL` before native
mutation. `keep_alive(60s)` enables it and `disable_keep_alive()` disables it.
There is no default delay; enable/disable intent is explicit.
Provide integral seconds (or an exactly convertible chrono duration); there is
no implicit fractional-second rounding. `UVPP_HAS_TCP_REUSEPORT` gates the
`reuse_port` bind flag; `ipv6_only` retains native IPv6 semantics.

On Windows, `simultaneous_accepts(false)` switches to a single pending accept;
a later `simultaneous_accepts(true)` returns native `UV_ENOTSUP`. Configuring the
initial enabled state succeeds; disabling it is not a reversible toggle there.

Open/bind/options and address queries reject closing handles with `UV_EBADF`.
Native descriptor/buffer queries retain native observation/error behavior after
initialization, including while closing if the native resource still exists.
Address queries preserve native outcomes: depending on socket creation/bind/
connect state they can return an address (possibly port zero) or a native error.
Address string formatting/conversion is a separate C++ operation and can allocate.

A successful `open` makes the handle responsible for closing the supplied socket;
do not independently close or use another owner to manage it. Failure leaves
ownership with the caller, but native preparation may have changed socket flags
or OS associations. Native bind/option failures likewise do not promise to undo
all native side effects. `sockname` and `peername` themselves use inline address
storage and introduce no string allocation.

## Ordinary and reset close

`close()`, `close(cb)`, and `close_static<Callback>()` return `void`, reflecting
native `uv_close`. Once initialized, start close only once. Close quiesces native
stream notifications and clears the stored listener/read/allocator callbacks.
An executing callback remains alive until return. The native close callback runs
later and may release the wrapper. Destructors do not close or drive the loop.

With `UVPP_HAS_TCP_CLOSE_RESET`:

```cpp
auto closing = socket.close_reset([](uv::raw::tcp& self) {
  // Native close has completed; self's storage may now be released.
});
if (!closing) {
  // No wrapper close callback was installed. Inspect closing.error().
  // The caller still needs a valid close strategy and retained storage.
}
```

`close_reset()`, `close_reset(cb)`, and `close_reset_static<Callback>()` return
`status`; they set reset-on-close semantics through libuv. Before init or while
closing they return `UV_EBADF`. Other rejection codes remain native; in particular
shutdown-in-progress conflicts can return `UV_EINVAL`.

On success, runtime close callback installation and stream-slot release are
committed after the native close initiation succeeds (native close is never
inline). On failure, the supplied runtime close callable is released and the
existing stream subscriptions are left intact. Normal close remains available
subject to its native lifetime requirements. Do not combine shutdown and reset
close contrary to libuv's restrictions. No native-side rollback beyond the
reported initiation outcome is promised.

Preparing any runtime `std::function` argument can allocate or throw before the
close/reset method is entered; that leaves native state untouched. Operational
failures are statuses, not converted `std::bad_alloc` or user-callable exceptions.
Exceptions escaping runtime/static C callbacks terminate at the native boundary.

## Stream and transitional request APIs

The handle inherits `uv::raw::stream<raw::tcp, uv_tcp_t>`.
[`listen`, `accept`, `read_start`, `read_stop` and `blocking`](raw-stream.md)
return statuses and share runtime/static replacement, stop and close cleanup.
Read results preserve borrowed buffers, EOF and byte counts. A pending read loan
can retain its subscription until its final native notification or close completion.

`connect` / `connect_static`, write and shutdown still retain throwing submission
and separate caller-owned `uv::connect_request`, `uv::write_request` and
`uv::shutdown_request`. Their completion statuses remain distinct from submission
exceptions. `write_now_result` retains bytes/would-block/error.

Requests and borrowed write/read buffers must survive their actual native
completion, including cancellation caused by close. Clearing handle-owned stream
slots does not clear request callbacks or permit early request destruction.
Their request `data` remains application-owned too. Request and handle storage
have different native layouts and reconstruction paths.

No new raw operation allocation, queue, owning resource hierarchy, coroutine
adapter, or callback payload copy is introduced. Existing C++ callback preparation,
legacy stream costs, and libuv backend allocations remain explicit and independent
of native operational error delivery. No measured allocation-free claim is made.
