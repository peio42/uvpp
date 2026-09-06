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

Represent write input policy through explicit borrowing, transfer of an owning
buffer, or a named copying helper. Avoid a single buffer type whose ownership
changes implicitly. For owned UDP results, copy the peer address as well as
retaining the payload. A buffer pool returns an owning lease; a view does not
extend the lease lifetime.

A high-level read subscription owns the read callback slots. Reject competing
readers or callback/coroutine subscriptions on the same stream. Start with a clear
single-consumer contract. Decide whether repeated reads keep the native watcher
running or stop it between awaits based on lifecycle tests and measurements.

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

## Implementation progress and validation

Existing views, owned buffers, and queue-size introspection are foundations. The
proposed read adapters, pools, and bounded flow-control layer are not implemented.

Validate EOF after partial input, zero-length datagrams, truncated datagrams,
consumer cancellation, callback conflicts, buffer reuse only after completion,
and slow consumers. Measure peak memory under sustained load and confirm it stays
within documented bounds, including in-flight operations.

## Owning Callback Operations

Owning callback stream writes are not implemented.
Explore a callback frontend owning its request state alongside the coroutine form.
Ownership of payloads must still distinguish borrowing, copying, and transfer; the
short signature alone cannot claim to retain borrowed bytes.
