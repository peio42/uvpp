# Lifecycle Validation and Performance Baselines

Status: partially implemented.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 exploration; independent compatible improvements may land in v2.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

The repository runs GCC and Clang tests on Ubuntu and includes native-layout tests.
There is no current cross-platform CI matrix or broad benchmark suite; the first
focused resource-scope cleanup measurement is described below. The [Makefile](../../Makefile) now generates and includes transitive header
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

For the initial task/ownership prototype, verify cold construction, root context
binding, inherited child context, single consumption, and rejection of cross-loop
joins and resource-affinity mismatches. Destroy active spawn handles while native
work cannot be cancelled, then verify retained state, actual completion, cleanup,
and unobserved-failure routing. Scope joins must precede destruction of external
borrowed data. Exercise the internal close-completion primitive and exceptional
scope exit without requiring a generic public coroutine close API.

For each subscription family, test slot release before terminal user delivery on
EOF where applicable, terminal error, completed cancellation, and explicit stop.
Install a replacement subscription from that delivery and verify that the old
callback path cannot clear its slots. Ordinary event delivery and temporary pauses
must retain a persistent subscription's claim. Verify buffer lifetime through actual
completion and the selected copying-helper timing in both callback and coroutine
frontends. These are future acceptance checks, not claims of implemented coverage.

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
`make test-asan-ubsan` builds a dedicated Clang AddressSanitizer/UndefinedBehaviorSanitizer
configuration with leak detection and fail-fast diagnostics; the same command runs in
CI. The sanitizer job is intentionally separate from the ordinary GCC/Clang matrix
so normal artifacts and sanitizer instrumentation never mix.

The coroutine suite now includes a bounded, deterministic TCP structured-lifecycle
stress case: repeated rounds establish several accepted connections, adopt them
with the listener into one resource scope, arm one final accept, then let
`finish()` quiesce that accept and close all dependent connections. It asserts
exactly-once `UV_ECANCELED` delivery and clean loop teardown. Loopback restrictions
produce an explicit test skip rather than a false network failure.

The coroutine suite also covers local-pipe connect failure and, where local pipe
bind is permitted, an outgoing pipe stream exchange, borrowed read/write slot
exclusivity, cancellation slot release, cross-loop rejection, and scoped close
completion. Sandboxes that prohibit Unix-domain pipe binding report these
transport tests as explicit skips.

`make measure-cleanup` builds and runs a dependency-free benchmark for repeated
multi-connection `resource_scope::finish()` calls. It reports C++ allocations made
while `finish()` is active (explicitly excluding libuv C allocations) and cleanup
latency min/mean/p95/max, along with compiler, libuv version, and workload size.
It establishes a reproducible collection method, not a numerical regression budget.

Platform jobs, focused TSan work, fault injection beyond the existing UDP case,
installed-package validation, and recorded cross-platform benchmark baselines remain
proposed. Acceptance requires those additions plus verified incremental rebuilding,
a working platform matrix, and stable regression budgets.

## Confirmed Feature-Gating Gaps

The current loop header references `uv_metrics_info()` and `uv_metrics_idle_time()`
unconditionally; filesystem `lutime` is also not gated. UDP `mmsg_chunk()` and
`mmsg_free()` deliberately return false when their flags are unavailable. Audit
these actual differences when defining a supported minimum libuv version; choose
explicit baseline requirements or capability gates and test both configurations.
Do not describe the existing macros as exhaustive older-version compatibility.
