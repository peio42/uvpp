# Lifecycle Validation and Performance Baselines

Status: draft.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

The repository runs GCC and Clang tests on Ubuntu and includes native-layout tests.
There is no current cross-platform CI matrix or benchmark suite. The [Makefile](../../Makefile) now generates and includes transitive header
dependencies for test objects and examples, including regeneration of missing
dependency files. This repair is an implemented baseline.

## Proposed design

Retain incremental dependency tracking checks for test objects and examples by
changing a header and observing rebuilds without requiring a clean build.
The isolated [UDP allocation test](../../tests/udp-allocation.cpp) already checks
copy-assignment rollback at each allocation failure; broaden lifecycle injection
coverage to the new owner and operation protocols.

Add sanitizer configurations (ASan/UBSan and focused TSan jobs), then supported
Windows and macOS builds. Adapt platform-specific tests rather than treating Linux
success as proof of portability. Establish a tested minimum libuv version and a
newer-version configuration for gated APIs; document unsupported combinations.

Prioritize lifecycle failure coverage: setup rollback, immediate submission failure,
late completion, cancellation failure, resubmission, destruction from callbacks,
partial I/O, and asynchronous close. Add controlled fault injection at narrow
internal boundaries without replacing all native integration tests with mocks.
Use bounded test timeouts to diagnose hangs; avoid sleep-only synchronization.

Compile every public header independently and link multi-translation-unit consumers.
Validate CMake consumption from an installed package and the packaged examples.
These checks matter for a header-only library and should include optional coroutine
headers once present.

Validate raw, high-level callback, and coroutine workflows together on one
`uv::loop`, with no required posting component. Compare throwing and `uv::ops`
submission/completion failures with equivalent ownership. Establish a usable
baseline before prototype 001; extend it through flow-control validation and API
freeze rather than waiting for every future benchmark before prototyping.

## Performance method

Keep repeatable libuv-direct, uvpp-static, uvpp-runtime, and coroutine workloads
where semantics and ownership costs are comparable. Report allocations, peak memory,
wrapper/frame size, throughput, latency distributions, binary size, and compilation
time. Separate setup from steady-state cost and library allocations from libuv's
own allocations. Record compiler, flags, platform, libuv version, and workload.

Include timer completion, stream transfers with slow consumers, filesystem/DNS
operation setup, and cross-thread posting. Use measurements to decide callback
storage, pools, shared deadline timers, and continuation scheduling. No numerical
speedup or allocation-free coroutine claim is made before those measurements.

## Alternatives and open questions

- Choose the benchmark harness without adding a mandatory library dependency.
- Decide stable regression budgets after collecting baseline variance.
- Select supported compiler/libuv versions and practical CI coverage.
- Keep optional focused includes; measure before considering modules or a compiled mode.

## Implementation progress and validation

Existing GCC/Clang tests and incremental dependency fixes are implemented.
Sanitizer configurations, platform jobs, and benchmarks remain proposed work, suitable
for v2 independently of v3. Acceptance requires reproducible commands, verified
incremental rebuilding, a working platform matrix, and recorded benchmark baselines.

## Confirmed Feature-Gating Gaps

The current loop header references `uv_metrics_info()` and `uv_metrics_idle_time()`
unconditionally; filesystem `lutime` is also not gated. UDP `mmsg_chunk()` and
`mmsg_free()` deliberately return false when their flags are unavailable. Audit
these actual differences when defining a supported minimum libuv version; choose
explicit baseline requirements or capability gates and test both configurations.
Do not describe the existing macros as exhaustive older-version compatibility.
