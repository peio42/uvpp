# Native Semantics and Compatibility Policy

## Principle

`uv::raw` is a low-level C++ interface to libuv. Its purpose is to expose libuv's capabilities while providing explicit C++ types, ownership rules, callback management, and error reporting.

**The raw layer must preserve libuv's documented semantics and must not implement compatibility workarounds for defects in particular libuv versions.**

A libuv defect should be reported and fixed upstream. Users affected by such defects should use a libuv version containing the fix.

## Behavioral fidelity

Raw wrappers must preserve the essential behavior of the corresponding libuv operations, including:

- Operation availability and native capabilities.
- Submission and completion semantics.
- Callback delivery, ordering, and error propagation.
- Stop, restart, cancellation, and close behavior.
- Native resource ownership and lifetime requirements.
- Platform-specific behavior and documented limitations.

The raw layer may expose stronger C++ interfaces where necessary to express its own contracts, such as typed results, explicit initialization, callback ownership, and safe buffer lifetimes.

Such adaptations must be deliberate, documented, and must not silently reinterpret native behavior.

Raw does not imply a strict one-to-one mapping of every C function, nor does it require exposing unsafe C++ lifetime patterns.

## No native bug workarounds

Do not introduce wrapper behavior solely to compensate for a defect in a supported or historical libuv version.

In particular, do not:

- Add artificial operation restrictions to prevent a known native bug.
- Modify undocumented libuv internal state to repair native behavior.
- Suppress, fabricate, reorder, or replay native callbacks to compensate for a defect.
- Add hidden retries, deferred operations, or cleanup mechanisms whose sole purpose is to mask a native defect.
- Preserve obsolete workarounds after their underlying justification has disappeared.

When a native defect is identified:

1. Confirm the behavior against libuv's documented contract and implementation.
2. Prefer reporting or fixing the defect upstream.
3. Document the affected versions and upstream fix when relevant.
4. Keep the raw wrapper's behavior consistent with the intended native contract.
5. Test corrected behavior against a libuv build containing the fix.

## C++ safety and wrapper-owned invariants

This policy does not prevent uvpp from enforcing invariants required by its own public C++ interface.

Examples include:

- Preventing use of uninitialized or closing wrappers.
- Preserving native handle address stability.
- Protecting callback and buffer storage while native operations may access them.
- Preventing premature reuse or destruction of wrapper-owned asynchronous request state.
- Rejecting an operation that conflicts with an explicitly documented uvpp ownership or subscription contract.
- Validating C++ inputs before native conversion where unchecked conversion would cause overflow or undefined behavior.

Such protections must address an actual wrapper-level contract or C++ safety requirement, not merely shield users from a native implementation bug.

An intentional restriction beyond libuv's documented behavior requires an explicit justification and review. Prefer placing higher-level policies in `uv` or `uv::ops` rather than `uv::raw`.

## Native implementation details

Avoid dependencies on undocumented libuv implementation details.

An exceptional dependency may be accepted only when necessary to preserve an established wrapper invariant, after reviewing available public-API alternatives, affected libuv versions, portability, and failure paths.

Such exceptions must be documented, tested, and tracked for later removal or replacement. They must not become a general mechanism for repairing native defects.

## Version compatibility

Separate the following concerns:

- **API compatibility:** uvpp compiles against a declared range of libuv versions.
- **Native behavior:** libuv may contain version-specific bugs or platform limitations.
- **Validated behavior:** particular scenarios have been tested against identified libuv versions and platforms.

Compiling against a libuv version does not guarantee that all native operations are free of defects.

Tests for behavior corrected in newer libuv versions should run against an appropriate native build. A known upstream defect must not be treated as a uvpp regression.

Do not introduce version-specific behavioral branches solely to emulate an upstream bug fix.

## Review requirements

For each new or migrated raw API, review:

- Correspondence with libuv's documented behavior.
- Immediate errors and asynchronous completion.
- Native and wrapper-owned resource lifetimes.
- Callback replacement, reentrancy, stop, restart, and close.
- Version and platform differences.
- Any additional restrictions, hidden operations, or dependencies on native internals.

Record intentional differences and unresolved questions in the design documentation.

The policy applies primarily to `uv::raw`. Higher-level APIs may deliberately provide stronger abstractions, but must document the semantics and costs they introduce.