# Operation state

This page records implemented cross-family observations. It does not introduce a
public base class, concept, or event-source API. Proposed general operation
machinery remains in [proposal 004](../proposals/004-operation-state.md).

## Terminal delivery invariant

An operation that reaches terminal delivery first releases its operation claim
and cancellation registration, captures everything needed for delivery, and only
then resumes user code. A native callback must not follow state owned by the
resumed coroutine frame after that resumption.

Cancellation is not itself terminal delivery. If native work remains in flight,
its state and borrowed inputs remain alive and its slot remains occupied until
the actual terminal callback.

## Observed after poll

The listener, signal, fs-event, poll, and process families confirm a common
exclusive-consumer delivery invariant, but not a common source lifecycle:

| Family | Native source lifecycle | Pending outcome / result policy |
| --- | --- | --- |
| signal | Persistent subscription | One coalesced pending signal |
| fs_event | Persistent subscription | One coalesced pending outcome; flags merge, filename becomes unknown on ambiguity, failures take precedence |
| poll | Native source armed per wait | None |
| process | Terminal event | Exit result retained for the source lifetime |
| listener | Persistent native connection opportunity | None; a successful accept returns an independently owned connection |

For `fs_event`, callbacks received without a waiter follow bounded coalescing
with failure precedence: `success A -> error -> success B` retains only the
error. B is ignored until that failure has been consumed; `next()` is not a FIFO
journal of native callbacks. This applies to native errors and captured filename
materialization failures. See [filesystem notifications](../user/fs-events.md#pending-notifications).

These observations do not justify a shared public event-source abstraction.

The private [`one_shot_callback_slot<Args...>`](../../include/uvpp/detail/one_shot_callback_slot.hpp)
used by signal, fs-event, poll, and process states factors only exclusive claim, claim
ownership tests, release, and detach-before-delivery. `claim()` returns `false`
for a null context, a null callback, or an occupied slot, leaving the slot unchanged.
Ownership tests and `release()`
require an active matching claim; a null context never owns an empty slot.
Delivery clears both the consumer context and callback pointer before invoking
the callback and does not access the slot afterward: the callback may destroy its
containing state or claim the slot again. Payloads are forwarded to the callback;
the slot stores only the context and function pointers.

Native arming, cancellation, pending-event policy, payload retention, and close remain
family-specific. In particular, `cancel_waiter()` stays outside the helper:
signal and fs-event cancellation leave their subscriptions active, poll cancellation stops
native polling, and process wait cancellation leaves the process running and
preserves its eventual exit result. The existing listener `accept_slot` also
detaches its separate cancellation pointer before family-specific child cleanup.

The [slot tests](../../tests/test-one-shot-callback-slot.cpp) cover competing claims,
release ownership, exactly-once delivery, reentrant replacement, destruction from
delivery, and forwarding move-only payloads and references. Family lifecycle tests
continue to cover native arming, cancellation/reuse, retained events, and close.
