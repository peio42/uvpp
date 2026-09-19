# Callback strategy

This page records callback machinery present on the v3 branch and the boundary
with the unfinished raw/high-level migration. See
[proposal 004](../proposals/004-operation-state.md) for the shared-operation target.

## Current dispatch

Low-level wrappers provide runtime callables and selected `template<auto Callback>`
static entry points. Runtime slots use copyable `std::function`; move-only callable
storage remains in [proposal 008](../proposals/008-move-only-callbacks.md).
Static dispatch avoids storing a callable, but does not remove string preparation,
native work, wrapper slot size, or other operation costs.

Trampolines reconstruct state from native storage layout. They must not use libuv
`data` for wrapper internals. High-level owners have their own stable state layout;
never recover a low-level wrapper from a high-level owner's native pointer.

`detail::invoke_callback` and static invocation catch escaping user exceptions and
terminate. Native callbacks are `noexcept`. Coroutine promises capture task exceptions
for observation at awaits or through spawn handles; exceptions never cross C frames.

## One-shot completion

Request terminal callbacks extract and clear their callable before invoking user
code. Release or transfer owned native results even when no callable is installed.
Clear submission inputs before user delivery when their lifetime has ended, and
avoid touching request state after delivery: the user may destroy or reuse it.

Existing submission helpers roll callback/input state back when native submission
fails, but do not cover every earlier allocation or callable-construction failure.
They currently throw on native failure and cannot be copied unchanged into the
v3 raw explicit-result surface.

## Persistent slots and owner operations

Persistent low-level callbacks and one-shot request extraction are distinct protocols.
Do not infer safe replacement or rollback from a similarly named start method:
audit the concrete slot and native start/stop behavior. Existing low-level APIs
are not yet a uniform v3 subscription frontend.

The v3 network-owner operations claim exclusive slots. A terminal read/receive
stops its native source and releases read/allocation/cancellation claims before
resuming the task. A submitted write/send retains its claim and borrowed payload
until actual native completion, even after stop. IPC variants share the stream
slots; see [I/O concurrency](io-concurrency.md).

A future persistent subscription must retain exclusivity between ordinary events
and release it only after terminal quiescence. Keep callback state alive through
in-flight native delivery, and prevent an old completion from clearing a slot
installed by resumed user code. This broader frontend remains proposed.

## Close completion

High-level close state joins repeated waiters around one native close. Native
completion marks the state closed and snapshots waiter delivery before resuming
user code. Storage needed by that callback survives owner release. Listener close
first quiesces its pending accept and provisional child, while connection/UDP close
rejects incompatible active I/O.

See [ownership](ownership-strategy.md), [errors](error-handling-strategy.md), and
[coroutine lifecycle tests](../../tests/test-co.cpp).
