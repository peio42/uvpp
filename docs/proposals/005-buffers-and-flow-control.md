# Buffer Ownership and Flow Control

Status: partially implemented.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 development on `main`.

This proposal is not completely implemented or an API-freeze commitment. Names
outside the implementation-progress section remain provisional.

## Motivation and current behavior

Streams and UDP expose allocation and receive callbacks with borrowed results.
Filesystem callback and coroutine reads and writes can borrow caller storage;
filesystem callback reads also have an explicit owned-storage form. These are
useful controls but require users to build buffer recycling and memory limits
themselves.

## Proposed design

Before prototype 001, define the minimum borrowing, copying, transfer, and
completion-lifetime rules below. Full read adapters, recycling, and bounded flow
control are validated after the prototype and before API freeze.

Keep the two-callback native read API in `uv::raw`. Add optional `read_some` into borrowed
mutable storage and an owning read form, plus `read_exactly` with explicit partial
data on EOF or error. These names are sketches, not available API. Distinguish
normal EOF, empty native notifications, bytes transferred, and actual errors.

Borrowing is the default payload policy for asynchronous write/send operations.
Copying or ownership transfer uses an explicit semantic name or type. Illustrative
coroutine spellings (not implemented signatures):

```cpp
co_await stream.write(data);      // borrow until actual completion
co_await stream.write_copy(data); // explicitly request a copy
```

Use `write` and `send` for the normal borrowing forms; no `_borrowed` suffix is
needed. Document that borrowed bytes must remain alive, address-stable, and
unmodified until actual completion, even after a cancellation request. Owning
operation state or retaining a coroutine frame does not retain external borrowed
bytes. Deferred operation construction must also document the input lifetime
required before submission. For copying helpers, specify whether the copy happens
at construction or startup and when the caller may release or modify its input;
this timing remains an open decision coordinated with [006](006-errors-and-results.md).

Avoid a single buffer type whose ownership changes implicitly. For owned UDP
results, copy the peer address as well as retaining the payload. A buffer pool
returns an owning lease; a view does not
extend the lease lifetime.

A high-level read subscription owns the read callback slots. Reject competing
readers or callback/coroutine subscriptions on the same stream. Start with a clear
single-consumer contract. Decide whether repeated reads keep the native watcher
running or stop it between awaits based on lifecycle tests and measurements.

On EOF, terminal error, completed cancellation, or explicit stop, quiesce the
native source and release both allocation and read/receive slot ownership before
delivering terminal completion to user code. An ordinary `next()` completion does
not terminate a persistent subscription: it retains its slots between events,
including when paused between awaits. A stop request alone does not free slots.
Keep state needed by in-flight callbacks alive, and never let an old completion
clear a replacement subscription installed by resumed user code. Apply the
family-specific terminal protocol in [004](004-operation-state.md).

Provide bounded queues with documented byte and item limits, and writer suspension
at configured thresholds. Account for in-flight storage as well as queued storage.
A write becoming complete means the native write contract is satisfied, not that a
remote application has consumed it.

For stream reads, pausing can limit local accumulation. UDP and filesystem watchers
cannot promise lossless delivery simply by pausing. Require an explicit overflow
policy: report overflow, drop newest/oldest, or stop the subscription as appropriate.
Do not silently create unbounded channels.

## Implementation and costs

Expose owning adapters on high-level `uv` objects, with explicit-result operations
in `uv::ops`. Error-policy selection must not change buffer ownership or require
caller-owned requests. Keep semantic ownership names; do not add `async_*` just
to distinguish error policies.

Build callback and coroutine forms over the same buffer owner and subscription
protocol. Pool allocation and metadata are opt-in and reusable. Preserve direct
scatter/gather APIs without hidden per-call metadata allocation. A read limit or
cancellation retains buffer storage until the native operation is finished.

## Alternatives and open questions

- Choose pool/allocator customization and lease ownership without mandatory shared pointers.
- Specify `read_exactly` partial-result shape and concurrent writer ordering.
- Decide whether async sequences or channels are needed beyond one-shot reads.
- Specify how pending producers and consumers wake on close or failure.
- Choose copying-helper copy timing and pre-submission input lifetime explicitly.

## Implementation progress and validation

Existing views, owned buffers, and queue-size introspection are foundations. The
callback filesystem facade now provides borrowed `read`/`write`, explicit
`read_owned`, and `write_copy`; its request state never owns borrowed bytes.
The proposed read adapters, pools, and bounded flow-control layer are not
implemented.

The experimental `uv::tcp_connection::write(std::string_view)` is the first
coroutine write primitive. It borrows the passed characters until actual
`uv_write` completion, including an error completion; `write_copy()` is not
implemented. One pending writer is supported per experimental connection.
Scoped cancellation is now exposed, but a submitted TCP write remains physically
non-cancellable: its borrowed characters stay alive, address-stable, and unchanged
until the actual completion callback after a stop request. A stop already requested
before submission delivers `UV_ECANCELED` without borrowing the buffer.

The experimental `uv::tcp_connection::read_some(std::span<std::byte>)` borrows
caller storage until a terminal data, EOF, or error notification. It returns a
count plus EOF flag; errors throw at the await. It is one-shot rather than a
persistent subscription and supports one active reader per connection.

The experimental `uv::pipe_connection` uses the same stream borrowing contract:
`write()` borrows characters through `uv_write` completion, while one-shot
`read_some()` borrows mutable storage until data, EOF, error, or cancellation has
quiesced `uv_read_start` and released its callback slots. It supports one reader
and one writer per connection; submitted writes are not physically cancellable.

For the initial IPC transfer slice, `pipe_connection::write_with_handle()` also
borrows its byte payload until `uv_write2()` completion and pins the exported TCP
owner for that same interval. `receive_handle(buffer)` borrows mutable caller
scratch storage while it reads until libuv reports a pending TCP handle, then
returns one freshly adopted stable owner after the terminal callback has stopped
the read. It is deliberately a dedicated-control-pipe primitive, not a framed
transport: bytes and pending handles cannot be associated. Bytes before the
handle-bearing callback accumulate in the caller buffer; the returned count covers
the complete accumulation. Exhausting that buffer before a handle is visible is
`UV_ENOBUFS`. If several TCP handles are pending, the native queue is drained into
one result and the caller takes its TCP owners one at a time. An
unsupported pending type fails closed rather than leaving a poisoned pending entry
in the pipe.

The experimental `uv::udp_socket::send_to()` likewise borrows its payload until
actual send completion; a stop already requested rejects submission, while an
in-flight send remains non-cancellable and retains the borrow. `recv_from()` is
one-shot and exclusive: it borrows mutable caller storage until the first actual
datagram, copies the peer address into the result, calls `uv_udp_recv_stop()`, and
releases receive/cancellation slots before task delivery. Empty libuv receive
notifications do not complete the await; an empty datagram does. UDP send and
receive use independent exclusive slots, allowing one of each to overlap while
rejecting another operation in either direction with `UV_EBUSY`; see the
[implemented concurrency contract](../design/io-concurrency.md).

Validate EOF after partial input, zero-length datagrams, truncated datagrams,
consumer cancellation, callback conflicts, buffer reuse only after completion,
copy timing, terminal subscription replacement, ordinary-event slot retention,
and slow consumers. Measure peak memory under sustained load and confirm it stays
within documented bounds, including in-flight operations.

## Owning Callback Operations

Owning callback stream writes are not implemented.
Explore a callback frontend owning its request state alongside the coroutine form.
The same default borrowing and explicitly named copying/typed transfer policy
applies to callback writes. Owning the request state does not retain borrowed bytes.

## Filesystem-event implementation progress

`fs_event_source` copies callback filenames into owned values. One pending
outcome coalesces flags and retains a name only when all names agree. Missing or
different names remain ambiguous until consumption. A failure replaces pending
success and remains sticky until consumption; this is bounded notification
retention, not a lossless history or general flow-control queue.
See the [implemented contract](../user/fs-events.md). Resource-scope adoption
and broader platform/cost validation remain open.

## Shared buffer header placement

The implemented `uv::buffer_view`, `uv::owned_buffer`, native `uv_buf_t`
construction, and length/count validation now live in
[`core/buffer.hpp`](../../include/uvpp/core/buffer.hpp). Filesystem operations,
streams, and network owners include this shared header directly. The former
`net/buffer.hpp` include path was removed without a forwarding header during v3
development. Namespaces, layouts, ownership, validation behavior, and allocation
costs are unchanged. This relocation does not implement the remaining owning adapters,
pools, or flow-control features proposed above.

Standalone inclusion of the new header compiles with GCC and Clang. The buffer
tests include it directly. Local `make test-all GTEST_ARGS=--gtest_brief=1`
passes both complete compiler suites (390 main cases passed, two TTY skips per
compiler), both isolated allocation cases, and example builds. The moved header
is byte-identical to its former location.

## Raw stream subscription progress

The persistent `uv::raw::stream` slice now pairs allocation/read callback ownership
across native buffer loans. Stop/replacement releases the active generation while
an outstanding loan keeps its original pair through final read delivery, the next
native allocation, or close completion. Terminal reads stop before user delivery;
normal/reset close retires pending pairs before user close callbacks. Callback
preparation can throw independently of native status failures. Reader/listener
reentrancy and runtime/static replacement are covered by
[stream tests](../../tests/test-raw-stream.cpp). Request migration is recorded below; buffer pools and
higher-level flow control remain separate work. See the
[implemented contract](../user/networking/raw-stream.md).

## Raw stream/TCP request progress

Connect/write/shutdown requests now live separately in `uv::raw`, with immovable
application-owned storage and `pending()` state. Stream write/shutdown and TCP
connect return submission statuses. Pending reuse is `UV_EBUSY`; native rejection
retains no new callback and produces no completion. Runtime/static selection is
extracted and pending cleared before delivery, allowing reentrant reuse without
old completion clobbering it. Handle close retains request/payload lifetimes until
native completion. Typed immediate write/read outcomes remain distinct. Pipe-specific connect/IPC interfaces now return statuses over the shared request
state; see the pipe progress section below. See the
[implemented request contract](../user/networking/raw-requests.md) and
[real request tests](../../tests/test-raw-stream-requests.cpp).

## Raw pipe progress

The caller-owned pipe wrapper is now `uv::raw::pipe`, with explicit status init,
throwing loop constructors, native-loop initialization tracking, common stream
close cleanup, status bind/open/configuration/connect and runtime/static IPC
submission. Name queries retain string payloads as results and preserve native
binary-name lengths; C++ allocations and legacy name copies retain exception
contracts. Counts/types and immediate write progress remain typed observations.
Raw request pending/reuse/completion and borrowed payload/exported-handle lifetimes
are shared with stream/TCP. High-level pipe owners are unchanged. The [raw UDP migration](../user/networking/raw-udp.md), including send/receive/request protocols, is now implemented. See [implemented pipe behavior](../user/networking/raw-pipe.md)
and [raw pipe tests](../../tests/test-raw-pipe.cpp).

## Raw UDP I/O progress

The [implemented raw UDP contract](../user/networking/raw-udp.md) now covers
status receive start/stop and asynchronous send submission, separate
`uv::raw::udp_send_request`, runtime/static callback replacement/reuse and close.
Paired subscription loans preserve allocator-owned storage through native delivery
and recvmmsg notifications actually delivered by libuv. Stop/replacement/close
from a chunk do not force remaining chunks or FREE; native callback fields are
left to libuv. If FREE is suppressed, the pending pair is retained until the next
allocation or close completion. Native receive errors quiesce before delivery. Immediate sends and suffix batches preserve
byte/datagram progress and would-block without hidden C++ operation metadata.
See [real native tests](../../tests/test-raw-udp-io.cpp) and the
[isolated allocation tests](../../tests/udp-allocation.cpp). Broader proposal work
remains open; high-level UDP owners and their operation state are unchanged.

## Raw TTY progress

`uv::raw::tty` now shares explicit status initialization, stream subscriptions
and separate write/shutdown requests with other raw stream handles. Mode returns
status, dimensions retain a typed result, and process-wide reset/virtual-terminal
operations do not depend on handle lifecycle. Native initialization failure
cleanup is backend-specific; the wrapper restores the uninitialized sentinel and
application data without changing libuv registration. See the
[implemented TTY guide](../user/tty.md) and [native tests](../../tests/test-tty.cpp).
Broader proposal work remains open.

## Inline Windows TTY read startup

Libuv's Windows TTY line-mode `uv_read_start` may synchronously invoke allocation
and deliver `UV_ENOBUFS`. Raw stream publishes the paired subscription before
native entry and rolls back a rejected start only if that generation is unchanged.
Inline stop/rearm/close is therefore preserved. The Windows console child test
exercises the native inline error and reentrant restart; ordinary stream tests
continue to cover rejection and borrowed-buffer lifetimes. Global Windows vterm
operations require prior libuv/loop initialization, not a TTY handle. See the
[raw stream guide](../user/networking/raw-stream.md) and [TTY guide](../user/tty.md).
