# Buffer Ownership and Flow Control

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Streams and UDP expose allocation and receive callbacks with borrowed results.
Filesystem reads can return owned storage. Low-level writes borrow payloads until
completion. These are useful controls but require users to build buffer recycling
and memory limits themselves.

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
proposed read adapters, pools, and bounded flow-control layer are not implemented.

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

The experimental `uv::udp_socket::send_to()` likewise borrows its payload until
actual send completion; a stop already requested rejects submission, while an
in-flight send remains non-cancellable and retains the borrow. `recv_from()` is
one-shot and exclusive: it borrows mutable caller storage until the first actual
datagram, copies the peer address into the result, calls `uv_udp_recv_stop()`, and
releases receive/cancellation slots before task delivery. Empty libuv receive
notifications do not complete the await; an empty datagram does.

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
