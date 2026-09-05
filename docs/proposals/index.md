# Proposals

These documents describe future or incomplete changes. They do not override the
[current design](../design/index.md) or document currently supported user APIs.
All proposals below are drafts for v3 exploration, not an agreed release scope.
Compatible work, especially validation improvements, may land independently in v2.

## Catalogue

| Proposal | Status | Scope |
| --- | --- | --- |
| [001 — Coroutines](001-coroutines.md) | Draft | Tasks, awaitables, repeated events, startup and continuation contracts |
| [002 — Asynchronous ownership](002-async-ownership.md) | Draft | Stable resource owners, close, and exceptional cleanup |
| [003 — Cancellation and task scopes](003-cancellation-and-task-scopes.md) | Draft | Stop, deadlines, joining, and structured composition |
| [004 — Shared operation state](004-operation-state.md) | Draft | Callback/coroutine submission, rollback, and completion machinery |
| [005 — Buffers and flow control](005-buffers-and-flow-control.md) | Draft | Ownership, read adapters, recycling, and bounded queues |
| [006 — Errors and results](006-errors-and-results.md) | Draft | Error vocabulary, result adaptation, and non-throwing names |
| [007 — Loop scheduling](007-loop-scheduling.md) | Draft | Cross-thread posting, continuation execution, and shutdown |
| [008 — Move-only callbacks](008-move-only-callbacks.md) | Draft | Callable ownership, replacement, and storage costs |
| [009 — Lifetime diagnostics](009-lifetime-diagnostics.md) | Draft | Optional lifecycle checks and instrumentation |
| [010 — Validation and performance](010-validation-and-performance.md) | Draft | Lifecycle tests, portability, sanitizers, builds, and benchmarks |

## Shared Constraints

Retain C++20, header-only integration, explicit native access, user-owned libuv
`data`, and stable native addresses. Preserve caller-owned low-level handles and
requests. Optional ownership, synchronization, and coroutine layers must state their
allocation and lifetime costs. A movable owner does not make its handle movable.
Do not promise allocation-free coroutine execution or speedups without measurements.

## Dependency and Implementation Order

1. Establish lifecycle validation and performance baselines (010).
2. Set operation, ownership, cancellation, error, and scheduling contracts
   (002, 003, 004, 006, 007), then prototype them together with coroutines (001).
3. Validate networking and filesystem workflows with explicit buffers (005),
   including early failure and scope cleanup, before freezing public coroutine APIs.
4. Introduce callback storage changes (008) and diagnostics (009) with focused
   lifecycle tests. Optimize pools, storage, and dispatch only against measurements.

This is a dependency guide, not a release schedule. Foundations already present in
v2 are recorded in each proposal and do not mean the proposed extension is implemented.

## Proposal Lifecycle

Use stable numeric identifiers and descriptive filenames. Each proposal records
motivation/current behavior, proposed public and internal contracts, ownership,
errors and costs where relevant, alternatives, open questions, compatibility or
version target, implementation progress, and validation criteria.

Statuses are **draft**, **in discussion**, **accepted**, **partially implemented**,
**implemented**, **rejected**, and **superseded**. Update this catalogue with the
proposal's status. Acceptance records a decision, not API availability. Record
unresolved details explicitly instead of presenting sketches as current examples.

For partial implementation, list completed and remaining work with code/test links.
As each part lands, update `docs/design/` to describe what exists and `docs/user/`
to explain its supported use. When complete, retain the proposal's rationale and
link to those authoritative documents. Rejected or superseded proposals remain as
history and link to replacements when applicable.

The coroutine proposal replaces the former `docs/design/coroutine-strategy.md`;
proposal 006 incorporates the former `docs/design/v3-notes.md`. The user-facing
[future features page](../user/future.md) points here rather than maintaining a
separate roadmap.
