# Optional Lifetime Diagnostics and Instrumentation

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

Address stability and native pointer recovery already have layout tests, but most
lifetime misuse is a caller contract violation with little context. Coroutines and
owners introduce additional transitions worth making visible during development.

## Proposed design

Treat diagnostics as transversal validation of raw lifetimes, high-level ownership,
and coroutine operation transitions on the common loop. Do not introduce another
ownership registry required in ordinary builds or a separate execution context.

Add an opt-in diagnostic mode with family-specific lifecycle checks: request
submitted twice, destruction while pending, handle use after close starts, double
close, incompatible read subscriptions, and access from the wrong thread.

Model initialization failure, stop, inactivity, closing, and completed close
separately. Inactivity alone does not permit handle destruction. Permit documented
request reuse from its completion callback. Define loop thread association when
the loop is run, including any supported sequential thread handoff.

Provide explicit hooks for submission, completion, cancellation, close, elapsed
time, and transferred bytes. Include operation identity and document hook argument
lifetimes. Hooks must not throw through C callbacks, recursively mutate the operation,
or require a logging framework. Borrowed observation must not extend ownership.

## Implementation and costs

Keep native storage layout invariants and user-owned `data`. Diagnostic fields or
an external registry must be compiled out when disabled; measure both modes. A
header-only configuration that changes layouts must be consistent across translation
units and documented as a program-wide setting to avoid ODR violations.

Native escape hatches bypass wrapper tracking. Define coverage limits and a way to
mark externally managed operations; diagnostics must not pretend to validate all
raw libuv use. Loop leak reports may list handles, while request tracking needs
separate bookkeeping.

## Alternatives and open questions

- Assertions, configurable violation handler, or structured diagnostic records.
- Registry versus per-object state and their costs.
- Hook safety, reentrancy restrictions, and timestamp overhead.
- How to identify raw interop without weakening ordinary checks.

## Implementation progress and validation

Layout assertions and native round-trip tests already exist. Lifecycle tracking and
instrumentation hooks are proposed; do not advertise current wrappers as checked.

Add misuse tests for each supported diagnostic and valid-lifecycle tests preventing
false positives. Verify callback resubmission, static paths, close completion, failed
initialization, and program-wide configuration across multiple translation units.
Confirm that disabled diagnostics add no tracking allocation or runtime hook calls.
