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

## Observed recurring shape

- persistent native source;
- one coroutine consumer slot;
- terminal delivery releases the slot before resuming the consumer.

Important semantic differences remain:

- listener: one native opportunity produces an independently owned connection;
- signal: a repeated source retains one coalesced pending notification;
- process: one terminal event is retained permanently after exit.

Do not introduce a shared public event-source abstraction yet. Revisit the shape
after `poll` or `fs_event` provides a second or third comparable high-level
family. Small private helpers remain appropriate only when they preserve these
family-specific semantics.
