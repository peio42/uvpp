# Move-Only Callbacks and Storage Costs

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Runtime callback slots use `std::function`, requiring copyable targets and possibly
allocating. A lambda owning a `std::unique_ptr` cannot be stored directly. Static
callback calls avoid an active stored callable, but the current wrapper still
contains its runtime callback members.

## Proposed design

Support owning move-only callables in runtime callback slots. Keep static callback
entry points and typed callback arguments. Constrain public templates with callback
invocability concepts to produce useful diagnostics. C++20 remains the baseline;
using a newer standard-library callable wrapper must not be a requirement.

Define and test one-shot versus persistent invocation rules. One-shot callbacks
are extracted and cleared before invocation. For persistent callbacks, replacing
or stopping a callback from inside itself must not destroy the callable currently
executing or restore an obsolete callback over its replacement. Preserve the current active-callable and generation-based replacement semantics
when replacing the storage implementation.

Submission failure must leave slots and owned captures in a documented state.
Switching runtime/static modes must specify when old captures are released. Keep
all C callback exception boundaries intact; changing storage does not change the
user exception policy.

This work is transversal to the architecture. It is a prerequisite only where
a selected operation or posting API requires move-only callback storage. Raw
static paths must not acquire coroutine or high-level ownership state.

## Implementation and costs

Prototype small-buffer owning type erasure with correct alignment, destruction,
and move behavior. Document inline capacity, overflow allocation, and setup failure.
Consider an explicit fixed-capacity variant for callers requiring a no-allocation
guarantee. A borrowed callable view is suitable only where its external lifetime
is explicit; it is not the default asynchronous callback owner.

Measure wrapper size and runtime cost separately. A static-storage-only wrapper
policy could remove unused slots, but do not proliferate public handle templates
without evidence that the savings justify API and compilation costs.

## Alternatives and open questions

- Small custom wrapper versus a dependency or optional newer-library backend.
- Handling oversized and over-aligned captures in fixed-capacity mode.
- Whether another persistent-slot design preserves current replacement semantics
  with lower measured storage costs.
- Whether static-only storage deserves a public opt-in type.

## Implementation progress and validation

Static callbacks and one-shot extraction already exist. Move-only runtime storage
requires implementation. Persistent replacement already uses an active callable
and generation tracking in [callback slots](../../include/uvpp/core/callback.hpp);
the new storage must preserve that behavior and its regression coverage.

Test unique ownership captures, small/large/over-aligned targets, replacement during
invocation, stop/restart, failed submission, callback destruction, and mode switches.
Benchmark registration allocations, wrapper sizes, dispatch, and code size against
both current `std::function` and static callbacks.

## Static Read/Receive Coverage

Stream `read_start_static` and an equivalent static UDP receive pair are absent.
Explore two static callbacks (allocation and read/receive), preserving the existing
borrow contract, rather than the former one-callback sketch. Validate buffer release
on EOF/error and UDP native chunk/free notifications. These additions could be
compatible with v2 and do not depend on move-only storage.
