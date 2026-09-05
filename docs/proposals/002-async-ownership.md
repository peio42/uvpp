# Asynchronous Resource Ownership

Status: draft.

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Low-level handles have stable addresses and require explicit close before
storage destruction. The current [ownership contract](../design/ownership-strategy.md)
is accurate, but every session must implement cleanup across success, submission
failure, callback failure, and cancellation. A final explicit close in the happy
path does not protect earlier exits.

## Proposed design

Add an optional unique owner for stable handle storage, plus asynchronous close
and a resource scope. A movable owner may transfer its pointer; the native handle
and low-level wrapper remain non-copyable and non-movable. Ownership transfer
must be named or represented by an owning type, never by an implicit borrowed view.

The scope retains registered resources until close completion, including when a
child task fails. Handle owners borrow their loop; the loop must remain alive and
be driven until cleanup finishes. Explicit asynchronous scope exit is the normal
path. The destructor cannot await, run a nested event loop, or immediately free a
closing handle. Before stabilizing this API, choose a documented fallback for an
owner destroyed without an explicit exit: transfer to an already-live cleanup
scope or diagnose the contract violation. Do not silently leak or detach cleanup.

A close operation claims the close callback slot and completes exactly once after
the native close callback. Reject incompatible existing close ownership. Repeated
close on the high-level owner may join the existing completion; it must not submit
another native close. Cancellation cannot undo close or release its storage early.

## Implementation and costs

Start with one explicit allocation for stable owned handle state. Avoid universal
shared ownership and reference counting. Consider scoped storage or pools only
after measuring this baseline. Apply the same ownership principles to file and
directory resources, while preserving their distinct native cleanup protocols.

Coordinate exceptional scope exit with [structured tasks](003-cancellation-and-task-scopes.md).
Cleanup failure must be observable without discarding the original task failure.

## Alternatives and open questions

- Caller-owned handles remain the allocation-free option.
- Decide owner construction, release, adoption, and borrowing vocabulary.
- Decide how multiple close waiters and cleanup errors are represented.
- Decide whether a resource scope and task scope are one public type or composed types.

## Implementation progress and validation

No v3 owner or resource scope is implemented. Existing close callbacks and filesystem
owners are foundations, not implementation of this proposal.

Validate normal exit, exception before the final close, failed initialization,
failed close submission for request-based resources, cancellation during cleanup,
owner moves, callback-slot conflicts, and loop shutdown with outstanding cleanup.
Use sanitizers to verify that storage survives every native completion.

## Remaining Directory Layer

The former design intentions for a higher-level directory owner belong here. V2
provides raw-only incremental directory iteration; its directory/result destructors
assert ownership has been consumed and do not close resources. Prototype an owning
incremental adapter with cleanup before every next read and close, owned entry names,
and an explicit asynchronous exit. Decide allocation/buffering and failure behavior
before promising a safe default directory API.
