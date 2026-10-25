# Raw UDP sockets

Include `<uvpp/handles/udp.hpp>` or `<uvpp/uv.hpp>`. `uv::raw::udp` owns inline,
address-stable native storage on the shared `uv::loop`. It is neither copyable nor
movable and has no `uv::udp` alias. Common addresses, `membership`,
`udp_socket_family`, `udp_init_flag`, `udp_bind_flag`, receive/immediate-send
results and UDP batch values remain in `uv`. The high-level `uv::udp_socket`
retains its independent stable storage and coroutine/`uv::ops` APIs.

Initialization, socket operations, receive subscriptions, asynchronous sends and
immediate sends now use the raw status/result protocols. The separate caller-owned
request is `uv::raw::udp_send_request`, without a legacy alias.

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

## Receive subscriptions

`receive_start(allocator, receiver)` and
`receive_start_static<Allocator, Receiver>()` return `status`. The allocator takes
`(udp&, size_t)` and returns `buffer_view`; the receiver takes
`(udp&, udp_receive_result)`. `receive_stop()` also returns `status`. Operational
entry points are `noexcept`; preparing runtime `std::function` arguments can still
allocate or throw before entry, leaving the existing subscription unchanged.
Empty runtime callables return `UV_EINVAL` before native changes.

A valid replacement first stops the old native receive source, then starts the
new one and commits its callback pair only on native success. A failed stop keeps
the old selection; a failed start after stop leaves reception stopped. A native
receive error stops the source and releases the active selection before delivery,
so the receiver may rearm. Repeated stop on a ready handle succeeds. Before init
or while closing, start/stop return `UV_EBADF`.

The allocator and receiver form one subscription. The pair that allocated a
buffer remains alive through its associated delivery, even when the receiver
stops, replaces itself or starts close. Mutable callable state survives repeated
delivery without copies. Generation tracking prevents an old callback from
restoring itself over a replacement. Allocators must only supply storage: they
must not stop/replace reception, close the handle or run the loop recursively.
Those transitions belong in the receiver callback.

`udp_receive_result` preserves signed `count()`, borrowed `address()`, `storage()`
and payload `bytes()`, truncation via `partial()`, and recvmmsg flags. An empty
native event has count zero and no address; an empty datagram has count zero and
an address. Test `mmsg_free()` separately before treating an empty event as a
packet. `raw_status()`, `error_code()` and `status()` normalize nonnegative counts
to success before narrowing. Receive errors retain the negative native count.
Borrowed addresses and payload observations are valid only during delivery.

With native recvmmsg enabled, a buffer may produce several `MMSG_CHUNK`
notifications followed by `MMSG_FREE` for the original allocation. Do not free the
allocation for individual chunks. Even after stop, replacement or close from a
chunk, the original pair receives the remaining chunks already read in that native
batch and its final FREE. A new subscription applies to subsequent allocations.
The wrapper keeps its fixed dispatcher installed long enough to finish that loan;
native polling remains stopped when requested. This matches the inspected Unix
libuv 1.45/1.48/1.53 chunk/FREE loops; backends without native recvmmsg use ordinary
receive delivery. A recvmmsg allocator must provide a sufficiently large buffer
(at least 64 KiB); `using_recvmmsg()` observes native support under its gate.

Stop/close release the active selection, but never release a pair still backing a
native buffer. Close completion releases any remaining loan before the close
callback. Socket storage remains alive through that completion.

## Asynchronous sends and requests

`send(request, payload, address, callback)` and
`send_static<Callback>(request, payload, address)` return submission `status`.
Payloads may be a `buffer_view`, `span<const buffer_view>` or
`span<const byte>`. Addresses may be IPv4, IPv6 or a native `const sockaddr*`;
omitting the address uses the connected peer (native connect capability required).
A single zero-length buffer sends an empty datagram. An empty scatter/gather list,
positive-length null view, invalid native count/length or empty runtime callback
returns `UV_EINVAL`. Socket lifecycle guards return `UV_EBADF`.

The immovable `uv::raw::udp_send_request` is distinct from the socket. It exposes
`pending()`, explicit native access and application-owned `data`. A pending request
rejects either submission mode with `UV_EBUSY` before input/handle validation,
without altering native storage, data or the accepted callback. There is no
replacement of an in-flight send callback.

Native rejection retains no callable and delivers no completion. Native acceptance
sets pending, then later invokes `(udp_send_request&, status)` with success or
failure. The wrapper clears pending and callback selection before user delivery
and never accesses the request afterwards. The callback can reuse or release the
completed request, including switching runtime/static modes. An executing old
callable survives through its return. Runtime callable preparation exceptions
remain C++ exceptions; native submission/completion errors remain statuses.

Payload bytes remain borrowed, stable and unmodified until actual completion.
Request storage survives completion and socket storage survives close completion.
Starting close does not release pending requests or borrowed payloads. Native
buffer descriptors and addresses are consumed/copied during submission, so their
metadata need not survive it. Libuv may allocate native request metadata; the
wrapper adds inline state without an operation allocation or hidden send queue.
All callback exceptions terminate at the native C boundary.

```cpp
uv::raw::udp_send_request request; // retain through completion
// socket is initialized; payload remains alive and stable through completion.
auto submitted = socket.send(request, payload, uv::ipv4{"127.0.0.1", 9000},
  [](uv::raw::udp_send_request &self, uv::status completed) {
    // self.pending() is false; self can now be reused.
    if (!completed) return;
  });
if (!submitted) {
  // No completion will arrive for this submission.
}
```

## Immediate sends and batches

`send_now(payload, address)` wraps native `uv_udp_try_send`; its spelling follows
the raw immediate-I/O convention. It accepts the same payload/address forms,
including omitted destination for a connected socket. It needs no request and
returns `send_now_result`: `ok()`, `bytes_sent()`, `would_block()`, `has_error()`,
`error_code()` and `raw()`. A successful empty datagram reports zero bytes, not
would-block. Check `would_block()` explicitly; its error-code accessor is empty
for this separate outcome. No error is flattened into a byte count or `status`.

Under `UVPP_HAS_UDP_TRY_SEND2`, `send_many_now(batch, first = 0)` returns
`send_many_now_result`, preserving a sent **datagram prefix count**, would-block
and native errors. It accepts an owning `udp_send_batch` or borrowed
`udp_send_many_view`; the native-array overload is also available. `first` selects
an unsent suffix without rebuilding metadata. Counts in the result are relative
to that suffix: add the count to `first` before retrying. A backend may report an
error for the entire native call instead of a partial prefix. The wrapper never
fabricates progress or automatically retries.

Before submission the wrapper validates all remaining datagram buffer lists and
top-level array pointers. Empty batches, `first >= count`, zero-buffer datagrams,
positive-length null buffers and unrepresentable counts return `UV_EINVAL` before
sending any packet. Zero-length buffers are valid empty datagrams. Destination
validity, connected-address rules and socket failures remain native outcomes.
The span-based borrowed-view constructor throws `invalid_argument` for unequal
metadata array sizes, independently of native submission.

`udp_send_batch` explicitly owns allocating descriptor/pointer vectors, not payloads
or addresses. Prepare/add/reserve/copy can allocate and throw; retain borrowed
payloads and addresses for each immediate call. Its `view()` is a read-only
metadata observation; the mutable `udp_send_many_view` borrows native arrays.
`send_many_now` itself neither allocates C++ metadata nor mutates the batch. The
isolated allocation tests exercise prepared heap-backed receive/send callables,
real native delivery and a prepared batch with C++ allocation disabled.
