# Raw stream requests

Include `<uvpp/handles/tcp.hpp>` or the stream/concrete handle header together
with `<uvpp/requests/write.hpp>`, `shutdown.hpp` or `connect.hpp` as needed.
The separate caller-owned types are `uv::raw::write_request`,
`uv::raw::shutdown_request` and `uv::raw::connect_request`, without legacy aliases.
They are neither handles nor owners: they are immovable request storage, with
explicit `native()`, `native_request()` and application-owned native `data`.
High-level TCP/pipe owners retain their independent operation states and APIs.

## Submission and completion

| Operation | Immediate return | Later callback |
| --- | --- | --- |
| `stream.write(request, buffers, callback)` | `uv::status` | `(write_request&, uv::status)` |
| `stream.write_static<Callback>(request, buffers)` | `uv::status` | Same arguments |
| `stream.shutdown(request, callback)` | `uv::status` | `(shutdown_request&, uv::status)` |
| `stream.shutdown_static<Callback>(request)` | `uv::status` | Same arguments |
| `tcp.connect(request, ipv4_or_ipv6, callback)` | `uv::status` | `(connect_request&, uv::status)` |
| `tcp.connect_static<Callback>(request, ipv4_or_ipv6)` | `uv::status` | Same arguments |

Write accepts a `span<const buffer_view>`, a single `buffer_view`, or a
`span<const byte>` in both runtime and static forms. Shutdown closes the writable
side after queued writes; it does not close the handle. Connect is a separate
operation on a previously initialized TCP handle. `accept` still uses a separate
client handle without a request.

```cpp
uv::raw::write_request request; // keep alive through completion
// socket is connected; payload also survives completion.
auto submitted = socket.write(request, payload,
  [](uv::raw::write_request &self, uv::status completion) {
    // self.pending() is already false; self can be reused here.
    if (!completion) return;
  });
if (!submitted) {
  // No completion callback will arrive for this rejected submission.
}
```

An accepted submission sets `pending()` to true, including when native work has
already finished but its callback has not yet been delivered. Its eventual status
is independent of submission success. Native rejection returns its code and leaves
the idle request reusable, with no retained callback and no future completion.
Uninitialized/closing handles return `UV_EBADF`. Empty runtime callbacks, empty
scatter/gather lists, positive-length null buffer views and unrepresentable native
buffer counts/byte lengths return `UV_EINVAL`. A single zero-length buffer is
allowed, subject to native acceptance.

Submitting a pending request returns `UV_EBUSY` before handle/input validation,
without modifying native storage, application `data` or the original callback.
There is no callback-replacement API for an in-flight request. Runtime/static paths
share this guard. At native completion the wrapper extracts the callback and clears
both pending state and selection **before** user delivery. No wrapper access to
the request follows delivery, so callbacks can resubmit the same request, switch
callback modes, or release completed request storage. An executing old callable
remains alive through return without overwriting a resubmitted operation.

All operations and request observations follow loop-thread affinity. Exceptions
from runtime/static completion callbacks terminate at the native C boundary.
Runtime `std::function` construction can allocate or throw before method entry,
even when a request is pending; this leaves its native operation unchanged.
Constructing buffer views or addresses has its own C++ validation/exception
contract. These failures are not translated into native statuses.

## Storage and allocation

Keep request storage and the stream's native handle alive until their respective
completion points. Starting handle close or requesting cancellation does not clear
`pending()` or release request callbacks. Completion reports native success or
failure, including cancellation, and only then retires the request. The handle
itself survives through native close completion.

Write borrows payload bytes: keep them alive, stable and unmodified until write
completion, including after close starts. Capturing a payload owner in the callback
can retain it; the wrapper does not copy or own borrowed bytes. Native descriptor
metadata and TCP addresses are consumed/copied during submission, so temporary
buffer-view arrays and addresses need not survive completion. Native metadata
allocation, when libuv needs it, is separate from C++ callback preparation. Request
state and callback selection are inline; there is no added per-operation C++
allocation or hidden queue. The isolated allocation test measures prepared write
submission and real completion with C++ allocation disabled.

`write_now` retains `write_now_result` with byte count, partial progress,
would-block and errors. It uses no request and does not flatten these outcomes into
`status`. Stream reads retain their own borrowed result and subscription protocol.

## Pipe requests

[`uv::raw::pipe`](raw-pipe.md) uses these same requests for inherited write/shutdown,
pipe connect/connect_static and IPC write/write_with_handle_static. All return
submission statuses and share the pending/completion protocol. Legacy pipe-name
preparation can allocate independently; void native connect reports errors at
completion. Pipe and TCP handles remain separate from their caller-owned requests.

## UDP send requests

`uv::raw::udp_send_request` uses the same pending/submission/completion protocol
with `udp.send` and `udp.send_static`. It is a separate immovable request, not a
handle. See [raw UDP](raw-udp.md#asynchronous-sends-and-requests) for borrowed
payloads, connected destinations, immediate sends and batch progress.
