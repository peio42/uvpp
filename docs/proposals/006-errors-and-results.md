# Errors, Results, and Operation Surfaces

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3; names and result shapes are provisional.

This proposal incorporates the former v3 naming notes and extends them to
composable operation results. It is not an implementation or a release commitment.

## Structural Error Surfaces

V2 keeps its current immediate-throwing and `try_*` conventions. V3 deliberately
changes the structure under [000](000-v3-architecture.md):

```cpp
// Proposed convention, not implemented signatures.
uv::foo(...);       // ergonomic / throwing
uv::ops::foo(...);  // explicit operation / status-oriented
```

Domain subdivisions such as `uv::ops::fs` follow the same rule. `uv::ops` operates
on the same high-level owners and does not require caller-owned native requests.
It is not a fourth layer. Raw operations in `uv::raw` expose native operational
failures through explicit status/results; construction and storage failures need
separate contracts.

Do not multiply `_status`, `_ec`, `try_`, or `async_` spellings solely to select
error policy. Keep semantic distinctions such as `write_borrowed`, `write_copy`,
and immediate `*_now` operations. In v3, reserve `try_*` for actual attempt-style
operations such as `try_lock()`.

Throwing immediate conveniences unwrap explicit operational results. For awaited
operations, the ergonomic facade raises operational failures at the await, while
`uv::ops` delivers typed results. Callback conveniences may throw at submission;
asynchronous completion is always delivered as a result. Neither policy can throw
back into a caller whose submission call has already returned.

Both surfaces use the shared protocol in [004](004-operation-state.md), preserving
the same ownership, native effects, cleanup, and completion guarantees.

## Unified Operation Results

Beyond naming, v3 should offer a common value/error vocabulary while retaining
specialized payload types for reads, datagrams, DNS, and filesystem results. A
`result<T>`-like vocabulary is an option, not a decision to replace all domain
results mechanically. Keep EOF, partial progress, and borrowed versus owned results
visible. Avoid exposing a successful value when only an error is present.

Explicit operations must cover native submission and completion failures through
the same result channel. Prefer deferred initiation or policy-aware construction
so setup does not submit and throw before the explicit policy takes effect.
An internal result adapter may implement the ergonomic facade, but a public
`as_result` spelling is not a second required error-selection API alongside
`uv::ops`. Exact callback and awaitable entry signatures remain to be prototyped.

Distinguish native operational failure from allocation failure and programmer misuse.
A result-returning API is not automatically `noexcept`; specify which setup failures
can still throw. Truly non-throwing variants must cover all failure paths, including
callback and input-storage setup. C++20 remains the baseline; `std::expected` must
not become a mandatory dependency on a newer standard.

## Exception Boundaries

Keep the existing low-level callback termination policy documented until an explicit
replacement is accepted. No exception escapes a libuv C callback. Coroutine promises
and operation state deliver observed failures through awaits. Independently spawned
tasks require an explicit error destination; the optional posting component may also accept an error
handler for posted callables. Define behavior if that handler throws. A loop-level
handler must not consume native `data` or silently change all callback semantics.

## Alternatives and Open Questions

- Domain/member facade spellings and explicit operation initiation need migration
  examples before acceptance.
- A generic value/error carrier versus a shared concept over existing domain results.
- Internal adaptation without duplicating public error-policy variants.
- Cleanup-error aggregation and allocation policy under the selected throwing /
  explicit-result surfaces.
- Whether exception-free builds are a separate supported target; this proposal alone
  does not promise support for disabling compiler exceptions.

## Implementation Progress and Validation

Current v2 immediate exceptions, asynchronous typed results, and `try_*` APIs are
implemented. The layered APIs, shared result adaptation, and unified value/error vocabulary
are proposed, not available. Target v3 because naming and result changes can break
source compatibility; publish a migration table before adoption.

Validate equivalent immediate/delayed native failures in both styles, allocation
failure during setup, EOF and partial results, move-only values, error access, and
unobserved-task routing. Test that native callbacks never propagate exceptions and
that result adaptation cannot miss failures during operation construction.

## Gaps Confirmed Against V2

The previous design text described one universal result interface, but stream/UDP
reads and filesystem watchers lack direct `raw_status()` and `error_code()` members;
poll has direct error access but no `raw_status()`. `uv::result::status()` returns
an integer, while domain `status()` returns `uv::result`. Filesystem success status
is normalized to zero. Uniformity is proposed here, not an existing guarantee.

The old `tcp.try_bind()` example also has no implementation. Consider non-throwing
bind only as part of a deliberately scoped immediate-error surface using the naming
decision above. Preserve existing `loop.try_close()` and request cancellation until
a breaking change is adopted.
