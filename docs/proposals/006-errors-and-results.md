# Errors, Results, and Operation Surfaces

Status: partially implemented.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3. The common vocabulary below is implemented; operation adaptation
remains incremental.

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
error policy. Borrowing is the default write/send payload policy: use `write` and
`send`, with explicit semantic names such as `write_copy` or owning types for
copying and transfer. Keep immediate `*_now` operations. In v3, reserve `try_*`
for actual attempt-style operations such as `try_lock()`.

Throwing immediate conveniences unwrap explicit operational results. For awaited
operations, the ergonomic facade raises operational failures at the await, while
`uv::ops` delivers typed results. Callback conveniences may throw at submission;
asynchronous completion is always delivered as a result. Neither policy can throw
back into a caller whose submission call has already returned.

Both surfaces use the shared protocol in [004](004-operation-state.md), preserving
the same ownership, native effects, cleanup, and completion guarantees.

The selected public owner-close API follows this split:

```cpp
co_await owner.close();
co_await uv::ops::close(owner);
```

The member await throws an operational `UV_EBADF` or `UV_EBUSY`; the `uv::ops`
operation reports the same conditions in its explicit result. Loop-affinity
violations remain programmer misuse outside that result channel, and allocation
while registering a joining close waiter may still throw. Native handle close has
no completion status to adapt. The operation remains non-cancellable once it has
started; see [002](002-async-ownership.md) for its state and lifetime contract.

## Unified Operation Results

V3 uses `uv::result<T>` and `uv::result<void>` as its common value/error
vocabulary. A result contains either one `T` or one `uv::error_code`; it never
exposes a value for an error result. `result<void>` represents success or an
operational error without a payload.

```cpp
uv::result<std::unique_ptr<connection>> connected{std::move(connection)};
uv::result<void> closed;
uv::result<void> refused{UV_ECONNREFUSED};
```

`result<T>` supports move-only values. It is copyable or movable exactly when
`T` permits the corresponding operation; it does not allocate. The primary
observation API is:

```cpp
if (result) {
  auto& value = result.value();
} else {
  uv::error_code code = result.error();
}
```

`has_value()` is equivalent to the explicit boolean conversion. `value()` has
`&`, `const &`, `&&`, and `const &&` overloads for value results. It throws
`uv::error` with the stored code when called on an error result; the `void`
specialization does the same. `error()` is non-throwing and returns an empty
`uv::error_code` for success. There are no `ok()`, `status()`, `canceled()`, or
`error_code()` synonyms on the common result type.

`uv::error_code` is a small libuv-specific value. `native()` exposes its
normalized native integer (zero or a negative libuv error), and `name()` and
`message()` expose libuv diagnostics; conversion to `std::error_code` is explicit.
`make_error_code(status)` normalizes all
non-negative statuses to an empty error code. The generic result uses this type
rather than a naked integer or an open-ended standard error category.

Specialized results remain necessary where the native completion has more state
than success-or-error. Reads retain EOF, byte count, partial progress, and their
borrowed buffer lifetime; immediate I/O retains would-block; datagrams, DNS, and
filesystem retain their typed payloads. Such types may expose a
`result<void>` status or eventually appear as `result<domain_outcome>`; this
milestone does not mechanically flatten them.

Explicit operations must cover native submission and completion failures through
the same result channel. Prefer deferred initiation or policy-aware construction
so setup does not submit and throw before the explicit policy takes effect.
An internal result adapter may implement the ergonomic facade, but a public
`as_result` spelling is not a second required error-selection API alongside
`uv::ops`. Exact callback and awaitable entry signatures remain to be prototyped.

An operational error is an expected error defined by the operation contract: a
native submission or completion failure, including a completed cancellation, is
reported by `uv::ops` as an error result. An input state such as a closed or busy
owner is also an error result when that operation documents it as a supported
state.

Loop-affinity violations, invalid lifetime or ownership use, double consumption,
and other broken API preconditions are programmer errors outside the result
channel. Diagnosable violations terminate rather than creating a recoverable
operational state. A result-returning API is not automatically `noexcept`:
allocation, user-provided constructors and moves, and documented C++ setup
failures can still throw. C++20 remains the baseline; `std::expected` is not a
dependency.

Copying-helper timing (construction versus startup) must be specified together
with setup-failure routing and the input lifetime before copying; see
[005](005-buffers-and-flow-control.md). Choosing an error surface must not change
that timing or the payload ownership policy.

## Exception Boundaries

Under `uv::ops`, native operational failures do not throw during setup,
submission, or completion: they become the operation's result. Exceptions that
remain are `std::bad_alloc`, exceptions from user-provided construction, move, or
callable code, other documented C++ preparation failures, and the explicit
`value()` access on an error result. The ergonomic facade unwraps the same
operational error at the await expression.

Keep the existing low-level callback termination policy documented until an explicit
replacement is accepted. No exception escapes a libuv C callback. Coroutine promises
and operation state deliver observed failures through awaits. A spawn handle
provides asynchronous join and result/exception observation. Destroying an active
handle requests cancellation while execution state survives through actual
completion and cleanup; it must not silently discard subsequent failures. Specify
an explicit destination for failures that can no longer be observed through join,
including failures left unobserved when a completed handle is destroyed. This
requirement applies even though public detach is deferred; see
[003](003-cancellation-and-task-scopes.md).

The optional posting component may also accept an error handler for posted
callables. Define behavior if an error handler throws. A loop-level
handler must not consume native `data` or silently change all callback semantics.

## Alternatives and Open Questions

- Domain/member facade spellings and explicit operation initiation need migration
  examples before acceptance.
- Internal adaptation without duplicating public error-policy variants.
- Cleanup-error aggregation and allocation policy under the selected throwing /
  explicit-result surfaces.
- Whether exception-free builds are a separate supported target; this proposal alone
  does not promise support for disabling compiler exceptions.

## Implementation Progress and Validation

`uv::error_code`, `result<T>`, and `result<void>` are implemented in
[`core/error.hpp`](../../include/uvpp/core/error.hpp). Completion statuses and
existing domain error accessors use the new error vocabulary. The paired
throwing/explicit operation surfaces are still being adapted one domain at a
time; target v3 because this changes source compatibility.

Validate equivalent immediate/delayed native failures in both styles, allocation
failure during setup, EOF and partial results, move-only values, error access, and
unobserved-task routing. Test that native callbacks never propagate exceptions and
that result adaptation cannot miss failures during operation construction.

## Gaps Confirmed Against V2

Specialized completion types still differ in their direct convenience accessors:
stream/UDP reads and filesystem watchers lack direct `raw_status()` and
`error_code()` members; poll has direct error access but no `raw_status()`.
Filesystem success status is normalized to zero. Their common nested status is now
`uv::result<void>`, but uniform domain adaptation remains future work.

The old `tcp.try_bind()` example also has no implementation. Consider non-throwing
bind only as part of a deliberately scoped immediate-error surface using the naming
decision above. Preserve existing `loop.try_close()` and request cancellation until
a breaking change is adopted.
