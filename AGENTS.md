# AGENTS.md

## Project

uvpp v3 is a header-only C++20 wrapper around libuv. This branch develops the v3
architecture and API; v2 is maintained on its own branch.

The repository and include root use the `uvpp` name. The public C++ API lives in
namespace `uv` and its subnamespaces:

```cpp
#include <uvpp/uv.hpp>

uv::loop loop;
```

## Design And Implementation Scope

- Design and implement toward the v3 contracts below. Read
  `docs/proposals/000-v3-architecture.md` and the relevant specialized proposals.
  V2 source compatibility does not constrain the target API on this branch.
- Use `docs/design/`, `docs/user/`, and the relevant code/tests to understand
  implemented behavior. Where they still describe v2, treat that as migration
  context rather than a rule overriding the v3 target.
- Proposals retain explicit status and open decisions. A draft is not an
  implemented API, a fully validated design, or authorization for an unrelated migration.
- When a task implements a proposed change, update the implemented documentation
  and proposal progress together. Keep the boundary between existing behavior and
  the target design explicit; do not resolve a conflict by silently mixing versions.

## Build And Test

- Build and run the default test suite with `make test`.
- Run both compiler suites with `make test-all`.
- Build examples with `make examples`.
- Build a local package with `make package VERSION=<package-version>` using the
  intended package version.
- Use C++20 and link applications with libuv and pthread.

## Coding Invariants

- Keep native handles and raw wrappers address-stable after native initialization.
  Do not make raw handles copyable or movable. A high-level owner may move by
  transferring stable storage without relocating native objects or invalidating callbacks.
- Do not store wrapper internals in libuv `data`; `data` belongs to application code.
- Keep raw libuv access explicit through named helpers such as `native()`, `native_handle()`, and `native_stream()`.
- Do not add implicit conversions to raw libuv pointers or borrowed view types.
- Produce borrowed views through explicit `.view()` functions, such as `timer.view()` or `buffer.view()`.
- Name synchronous immediate libuv operations `*_now()` even when the libuv function uses `try`, such as `send_now()` or `write_now()`.
- Use typed result objects when an immediate libuv return value mixes payload and status, such as byte counts plus `UV_EAGAIN`.
- Use C++20 concepts for public templates that depend on structural API contracts, such as stream-like handles or callback invocability.
- Prefer references in callbacks when null is not valid.
- Preserve typed outcomes, including EOF, partial progress, and would-block.
  Follow the target layer's error policy below for result delivery and throwing awaits.
- Use `std::chrono` for public duration values; keep counters as integer counts.
- Keep asynchronous lifetime visible through handle/request ownership.
- Keep allocation and ownership costs explicit. Do not add hidden operation
  allocations to raw primitives; document justified native-lifetime storage costs.
  High-level owners and coroutine frames may allocate. Measure allocation-free claims.
- Cancellation requests do not imply completion or permit early storage release.
- Retain native storage through close completion. Destructors must not run a
  nested loop to emulate asynchronous cleanup.
- Follow loop-thread affinity unless an entry point is explicitly cross-thread safe.
- Do not let exceptions escape from libuv C callbacks.

## API Policies

- Follow proposal 000: raw `uv::raw`, high-level `uv`, and coroutine primitives
  `uv::co` share `uv::loop`. `uv::ops` selects explicit-result operations on the
  same high-level owners; it is not another I/O or ownership hierarchy.
- Follow proposal 006 for error policy: raw operational failures use explicit
  status/results; ergonomic high-level awaits throw at the await expression;
  `uv::ops` awaits report submission and completion failures as results. Setup
  and allocation failures need their own contract. Status-oriented is not
  automatically `noexcept`.
- Reserve `try_*` for actual attempt semantics. Do not add error-policy suffixes
  such as `_status`, `_ec`, or `async_`. Borrowing is the default write/send payload
  policy; copying and ownership transfer use explicit semantic names or types.
- Follow proposals 001/003/007 for cold tasks, root context binding at spawn,
  inherited child context, spawn-handle ownership, cancellation, and asynchronous
  join. Public detach is deferred.
- Follow proposal 002 for required internal close completion and asynchronous
  scope cleanup; generic public coroutine close remains a separate decision.
- Follow proposals 004/005 for terminal callback-slot release before user delivery,
  persistent subscription exclusivity, and borrowed-buffer lifetimes.
- These are target contracts, not claims of available v3 APIs. Consult the proposals
  for remaining choices and validation gates rather than freezing sketches here.

## Documentation Layout

- User documentation and tutorials live in `docs/user/`.
- Implemented design and coding rules live in `docs/design/`.
- Future or partially implemented changes live in `docs/proposals/`, with explicit status.
- When implementing a proposal, update current design and user documentation and record progress in the proposal.
- Keep `README.md` focused on project overview, quick start, build, and links.
- Put user-facing explanations in `docs/user/*.md`, not in design strategy files.

## Design References

Before changing public API shape or callback/lifetime behavior, read
`docs/proposals/index.md`, `docs/proposals/000-v3-architecture.md`, and the
specialized proposals relevant to the change. The index defines dependencies and
proposal lifecycle; proposal 010 defines lifecycle validation and performance gates.

Also read the following to understand existing behavior and the migration needed.
Any remaining v2-specific policies are historical context for that migration:

- `docs/design/architecture.md`
- `docs/design/api-principles.md`
- `docs/design/api-policy-decisions.md`
- `docs/design/callback-strategy.md`
- `docs/design/error-handling-strategy.md`
- `docs/design/ownership-strategy.md`
- `docs/design/thread-safety.md`
