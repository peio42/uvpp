# Raw UDP sockets

Include `<uvpp/handles/udp.hpp>` or `<uvpp/uv.hpp>`. `uv::raw::udp` owns inline,
address-stable native storage on the shared `uv::loop`. It is neither copyable nor
movable and has no `uv::udp` alias. Common addresses, `membership`,
`udp_socket_family`, `udp_init_flag`, `udp_bind_flag`, receive/immediate-send
results and UDP batch values remain in `uv`. The high-level `uv::udp_socket`
retains its independent stable storage and coroutine/`uv::ops` APIs.

This first UDP slice migrates initialization and socket operations. Send requests,
receive subscriptions and their callback replacement/terminal protocols remain a
separate step; their presence in this header is not a claim of completed migration.

## Initialization and lifetime

```cpp
uv::raw::udp socket;
auto initialized = socket.init(loop);
if (!initialized) return;
auto bound = socket.bind(uv::ipv4{"127.0.0.1", 0});
if (!bound) {
  socket.close(); // retain storage until native close completion
  return;
}
```

Default construction does not register a native handle. `init(loop_or_view)`
uses libuv's lazy default initialization. With `UVPP_HAS_UDP_INIT_EX`,
`init(loop_or_view, udp_socket_family)` creates an IPv4/IPv6 socket eagerly.
The existing recvmmsg capability gate adds the family/`udp_init_flag` form.
Corresponding throwing constructors call the same init then `.throw_if_error()`.
All init forms return `status` and are `noexcept`.

`initialized()` observes the native loop pointer. Repeat init is `UV_EALREADY`,
including during/after close; it preserves native address, loop, family/flags and
application `data`. Successfully initialized storage cannot be reinitialized.
Native initialization failure leaves uninitialized, retryable storage with `data`
preserved. Unix creates its eager socket before registration; Windows removes a
failed registration and closes any new socket itself, leaving a stale loop pointer.
Uvpp clears that storage/sentinel without a second queue removal or asynchronous
close. The relevant paths were reviewed in official libuv 1.45, 1.48 and 1.53.
No native-lifetime allocation or poll-specific rollback is added.

Socket operations return `UV_EBADF` before init or while closing. Default-state
active/closing/ref observations are false; ref/unref/close are inert. Normal close
and its runtime/static callback forms are guarded against duplicate close. Retain
native storage through close completion and drive the loop normally; no destructor
runs a nested loop. Native `data` is application-owned. All methods follow
loop-thread affinity and exceptions cannot escape native callbacks.

## Socket operations and typed outcomes

| Operation | Outcome |
| --- | --- |
| `open(uv_os_sock_t)` | `status`; native socket ownership passes to libuv on success |
| `bind(ipv4_or_ipv6, unsigned_flags = 0)` / typed `udp_bind_flag` | `status` |
| `connect(ipv4_or_ipv6)`, `disconnect()` | `status`, with `UVPP_HAS_UDP_CONNECT` |
| `sockname()`, `peername()` | Const `noexcept -> result<socket_address>`; peer query has the connect gate |
| `set_broadcast(bool)`, `set_ttl(int)` | Native `status`, `noexcept` |
| `set_multicast_loop(bool)`, `set_multicast_ttl(int)` | Native `status`, `noexcept` |
| `set_multicast_interface(string_view)` / `set_multicast_interface()` | `status`; omitted interface passes native `nullptr` |
| `set_membership(group, interface, membership)` / `(group, membership)` | `status` |
| `set_source_membership(group, interface, source, membership)` / `(group, source, membership)` | `status`, with the source-membership gate |

UDP connect is a synchronous peer association without a connect request. It does
not establish a TCP-style connection or guarantee a remote receiver exists.
Native failures, supported flags, TTL ranges and multicast interface/routing
restrictions remain native outcomes. In particular, multicast TTL validation can
differ by backend. Bind/open/options may create/configure a socket before a later
failure; this slice does not promise rollback of arbitrary native side effects.

Addresses retain their payload and native family rather than being flattened to
status. Common descriptor and socket-buffer queries use the initializable-handle
result protocol shared with raw TCP/pipe. Send queue size/count and gated
`using_recvmmsg()` remain native observations while initialized, including while
closing; their defaults before init are zero/false.

## Multicast preparation

String-taking operations prepare terminated `std::string` copies before calling
libuv. These can allocate or throw independently of native `status` failures and
are not `noexcept`. Argument preparation errors are not translated into native
error codes. Embedded NUL bytes return `UV_EINVAL` before copying/submission rather
than silently selecting a different group/interface/source. An omitted interface
passes `nullptr`; an explicitly supplied empty string retains native interpretation.
Native multicast membership may depend on interface support and permissions.
The no-argument multicast-interface reset and numeric/bool setters do not prepare
C++ strings. There is no new per-socket C++ allocation or hidden I/O queue.

## Remaining I/O migration

`send`, `send_static`, `receive_start` and `receive_stop` retain their existing
throwing operational/submission APIs and `uv::udp_send_request`. Their callback
slot and pending/reuse protocols are not migrated by this socket slice. They now
reject uninitialized/closing handles before native entry, using their existing
throwing policy. No early request completion or new cancellation guarantee is
introduced. Receive callables keep their legacy retention through wrapper lifetime;
normal close does not yet implement the final paired UDP buffer-loan protocol.

`send_now` and gated `send_many_now` keep typed byte/datagram progress,
would-block and error outcomes, adding only the lifecycle guard. Receive results
retain empty datagrams versus empty native events, partial packets, borrowed
addresses/buffers and recvmmsg chunk/free flags. Batch/view helpers keep their
existing explicit allocation/borrowing contracts in `uv`. Payloads, requests and
native buffers remain alive through actual completion, independently of socket
options or starting close.
