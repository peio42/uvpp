# Proposals

These documents describe future or incomplete changes. They do not override the
[current design](../design/index.md) or document currently supported user APIs.
Except where marked accepted, proposals below are drafts for v3 exploration, not
an agreed release scope. Compatible work, especially validation improvements, may
land independently in v2.

## Catalogue

| Proposal | Status | Scope |
| --- | --- | --- |
| [000 — V3 architecture](000-v3-architecture.md) | Accepted | Layers, namespaces, common loop, ownership boundaries, and error surfaces |
| [001 — Coroutines](001-coroutines.md) | Draft | Tasks, awaitables, repeated events, startup and continuation contracts |
| [002 — Asynchronous ownership](002-async-ownership.md) | Draft | Stable resource owners, close, and exceptional cleanup |
| [003 — Cancellation and task scopes](003-cancellation-and-task-scopes.md) | Partially implemented | Stop, deadlines, joining, and structured composition |
| [004 — Shared operation state](004-operation-state.md) | Draft | Callback/coroutine submission, rollback, and completion machinery |
| [005 — Buffers and flow control](005-buffers-and-flow-control.md) | Draft | Ownership, read adapters, recycling, and bounded queues |
| [006 — Errors and results](006-errors-and-results.md) | Draft | Error vocabulary, throwing facades, and explicit `uv::ops` results |
| [007 — Loop scheduling](007-loop-scheduling.md) | Draft | Common-loop continuations, optional cross-thread posting, and shutdown |
| [008 — Move-only callbacks](008-move-only-callbacks.md) | Draft | Callable ownership, replacement, and storage costs |
| [009 — Lifetime diagnostics](009-lifetime-diagnostics.md) | Draft | Optional lifecycle checks and instrumentation |
| [010 — Validation and performance](010-validation-and-performance.md) | Draft | Lifecycle tests, portability, sanitizers, builds, and benchmarks |

## Shared Constraints

[000 — V3 architecture](000-v3-architecture.md) defines the proposed common
invariants: `uv::raw`, high-level `uv`, and coroutine `uv::co`, sharing `uv::loop`.
`uv::ops` is the high-level explicit-result surface, not another owner hierarchy.
Specialized proposals refine that architecture; current v2 policy remains in design.

Keep C++20, header-only integration, explicit native access, application-owned
`data`, and stable native addresses. Costs and asynchronous ownership remain
explicit. Positive breaking changes take precedence over v2 source compatibility.

## Dependency and Implementation Order

1. Establish the architecture and namespace/error/ownership boundaries (000).
2. Establish lifecycle validation and performance baselines (010).
3. Define initial ownership, cancellation, operation, error, and scheduling
   contracts (002, 003, 004, 006, 007), plus minimum buffer lifetime rules (005).
4. Prototype coroutines (001), feeding evidence back into those contracts.
5. Validate buffers and bounded flow control (005) in complete workflows.
6. Freeze the API after lifecycle, interoperability, and cost validation.

Callback storage (008) and diagnostics (009) advance transversally and become
prerequisites only where a chosen contract requires them. This is a dependency
guide, not a release schedule or a requirement to finish every foundation before
prototyping. Compatible validation work may land in v2 independently.

## Proposal Lifecycle

Use stable numeric identifiers and descriptive filenames. Each proposal records
motivation/current behavior, proposed public and internal contracts, ownership,
errors and costs where relevant, alternatives, open questions, compatibility or
version target, implementation progress, and validation criteria.

Active statuses are **draft**, **in discussion**, **accepted**, and
**partially implemented**. Acceptance records a direction, not API availability.
Record unresolved details explicitly instead of presenting sketches as current APIs.

For partial implementation, update `docs/design/` and `docs/user/` as each part
lands, and record completed foundations with code/test links where useful to the
remaining work. Keep the proposal focused on what still needs implementation.
When complete, migrate any remaining contract documentation, update incoming links
and this catalogue, and remove the proposal. Remove rejected or superseded proposals
after updating active references to their replacement or dropping obsolete links.

Git preserves history. Do not reuse retired numeric identifiers. If a decision's
rationale needs durable documentation beyond the implemented design, a separate
ADR may be introduced; an ADR system is not required to complete a proposal.
The proposals directory is an active work catalogue, not an archive.

The coroutine proposal replaces the former `docs/design/coroutine-strategy.md`;
proposal 006 incorporates the former `docs/design/v3-notes.md`. The user-facing
[future features page](../user/future.md) points here rather than maintaining a
separate roadmap.
