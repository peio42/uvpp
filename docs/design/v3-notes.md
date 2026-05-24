# v3 Design Notes

Status: exploratory notes for a future breaking release.

This file records API issues that are acceptable in v2 but worth revisiting if
uvpp ever makes a breaking v3 release. Notes here are not commitments.

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
