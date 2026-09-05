# Errors, Results, and Non-Throwing Names

Status: draft.

Target: v3; names and result shapes are provisional.

This proposal incorporates the former v3 naming notes and extends them to
composable operation results. It is not an implementation or a release commitment.

## Non-Throwing Variant Names

uvpp v2 reserves `try_*` for non-throwing variants of APIs that would otherwise
throw on immediate libuv failure. For example, a throwing `cancel()` operation
may have a `try_cancel()` variant returning `std::error_code`.

This convention is clear inside uvpp, but it conflicts with a common C++ use of
`try_*`: an operation that attempts normal work without blocking or waiting,
such as `try_lock()`.

v2 should keep the existing convention for compatibility. New synchronization
primitive wrappers should still use standard names such as `try_lock()` for
standard synchronization behavior.

For v3, consider replacing the general non-throwing naming convention with one
of these shapes:

- `_ec` suffix for simple error-code returns, such as `cancel_ec()`;
- `_status` suffix for explicit typed status/result objects, such as
  `bind_status()` or `cancel_status()`;
- a tag-based overload, such as `cancel(uv::as_status)`, if keeping the primary
  verb is more important than discoverability.

The preferred v3 direction is:

- keep primary APIs throwing for immediate submission failures where that
  remains uvpp policy;
- use `_status` for non-throwing variants that return typed result objects;
- use `_ec` only for simple variants that return `std::error_code` directly;
- reserve `try_*` for true attempt-style operations, especially synchronization
  operations such as `try_lock()`.

This would reduce ambiguity while preserving uvpp's explicit status-channel
style.

## Unified Operation Results

Beyond naming, v3 should offer a common value/error vocabulary while retaining
specialized payload types for reads, datagrams, DNS, and filesystem results. A
`result<T>`-like vocabulary is an option, not a decision to replace all domain
results mechanically. Keep EOF, partial progress, and borrowed versus owned results
visible. Avoid exposing a successful value when only an error is present.

Coroutine operations should share one vocabulary with an explicit result adapter
such as `as_result(operation)`. Decide the default throwing/result style during
prototyping. The result adapter must report native submission and completion failures
through the same channel. If construction performs submission before the adapter
runs, the contract is broken: defer submission or choose a policy-aware factory.

Distinguish native operational failure from allocation failure and programmer misuse.
A result-returning API is not automatically `noexcept`; specify which setup failures
can still throw. Truly non-throwing variants must cover all failure paths, including
callback and input-storage setup. C++20 remains the baseline; `std::expected` must
not become a mandatory dependency on a newer standard.

## Exception Boundaries

Keep the existing low-level callback termination policy documented until an explicit
replacement is accepted. No exception escapes a libuv C callback. Coroutine promises
and operation state deliver observed failures through awaits. Independently spawned
tasks require an explicit error destination; a scheduler may also accept an error
handler for posted callables. Define behavior if that handler throws. A loop-level
handler must not consume native `data` or silently change all callback semantics.

## Alternatives and Open Questions

- `_status`, `_ec`, or tag overloads for immediate operations: the preference above
  needs discoverability and migration examples before acceptance.
- A generic value/error carrier versus a shared concept over existing domain results.
- Result adapter versus separate names; the adapter is preferred for coroutines.
- The default coroutine error style, cleanup-error aggregation, and allocation policy.
- Whether exception-free builds are a separate supported target; this proposal alone
  does not promise support for disabling compiler exceptions.

## Implementation Progress and Validation

Current v2 immediate exceptions, asynchronous typed results, and `try_*` APIs are
implemented. The renamed APIs, coroutine adapter, and unified value/error vocabulary
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
