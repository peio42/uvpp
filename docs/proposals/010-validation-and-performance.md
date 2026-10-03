# Lifecycle Validation and Performance Baselines

Status: partially implemented.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 development on this branch.

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
and exactly-once routing through both the default and configured unobserved-failure
policies. Include multi-joiner abandonment, loop liveness through terminal native
cleanup, a one-shot request held to its native callback, and repeated synchronous
completion; use deterministic ordering controls rather than timeout-based stress.
Scope joins must precede destruction of external
borrowed data. Exercise the internal close-completion primitive, exceptional
scope exit, and the public coroutine close contract.

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
completion. Pipe-listener tests cover stable accepted-owner transfer, concurrent
accept exclusion, pending-accept cancellation, scope shutdown, and the direct
close violation. Sandboxes that prohibit Unix-domain pipe binding report these
transport tests as explicit skips.

The coroutine signal-source tests exercise persistent subscription across two
signals, one pending-notification handoff, current-wait cancellation followed by
reuse, concurrent-wait `UV_EBUSY`, and terminal close cancellation. They use
`SIGUSR1` and `raise()` on platforms where libuv delivers it; platform-specific
signal behavior remains part of the broader portability matrix.

The high-level `data` reconstruction slice retains the existing connect/accept,
IPC transfer, signal delivery, failed-construction, cancellation, and close tests.
Additional [UDP](../../tests/test-co-udp.cpp) and
[process](../../tests/test-co-process.cpp) cases move an owner while a receive or
exit wait is pending, then verify native address stability, result delivery, and
close completion. Raw layout and user-data preservation tests remain in place.

`make measure-cleanup` builds and runs a dependency-free benchmark for repeated
multi-connection `resource_scope::finish()` calls. It reports C++ allocations made
while `finish()` is active (explicitly excluding libuv C allocations) and cleanup
latency min/mean/p95/max, along with compiler, libuv version, and workload size.
It establishes a reproducible collection method, not a numerical regression budget.

`make test-tsan-posting` builds a separate GCC ThreadSanitizer binary containing
only posting tests and excludes forking death tests. Posting coverage compares
accepted versus delivered work across repeated producer/close races, checks FIFO
with live producers, and exercises surviving endpoints and reentrant capture
destruction. Full compiler and ASan/UBSan suites also include the posting close
and callback exception lifecycle tests. This focused target depends on a supported
host TSan runtime; it does not instrument the system libuv or GoogleTest libraries
and does not establish a cross-platform or minimum-libuv baseline.

The posting validation slice has passed locally on Linux with libuv 1.51.0:
both complete GCC/Clang suites, the complete Clang ASan/UBSan suite with leak
detection, and the focused GCC TSan target. The standalone posting example also
runs under ASan/UBSan. Sandboxed network restrictions and LeakSanitizer's ptrace
limitation require running the complete suites outside that sandbox; these are
host constraints, not skipped posting coverage.

Platform jobs, broader TSan CI work, fault injection beyond the existing UDP case,
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

## Poll-source implementation progress

The [poll-source tests](../../tests/test-co-poll.cpp) exercise inactive setup,
borrowed descriptor/non-blocking effects, repeated level readiness, mask changes,
waiter exclusion, cancellation/reuse, active owner moves, reentrant destruction,
close joins, invalid masks, and loop mismatch. Linux additionally exercises
native start conflict through both error policies; callback failure is injected
at the native delivery boundary. Windows validation, allocation measurements,
and broader native failure injection remain open.

On Linux with libuv 1.51.0, all 15 poll tests pass with GCC and Clang and in a
focused Clang ASan/UBSan build with leak detection. Both complete compiler suites
also pass, including the separate UDP allocation binary; two pre-existing
platform/environment tests are skipped. This is local validation, not a
cross-platform or performance guarantee.

Poll event capability gates now cover `UV_DISCONNECT` (libuv 1.9.0) and
`UV_PRIORITIZED` (libuv 1.14.0), including the public enum, per-wait mask
validation, and affected tests. This does not resolve the remaining minimum-libuv
compatibility gaps listed above. GCC and Clang compile checks at simulated
1.8/1.9/1.13/1.14 header versions, with unavailable poll constants masked, pass;
these checks are not builds against actual older libuv headers.

## Filesystem-event implementation progress

The [fs-event tests](../../tests/test-co-fs-event.cpp) cover owned callback names,
flag merging, irreversible ambiguity and missing names, one-time pending
consumption, cancellation/reuse, pre-existing stop preserving pending state,
waiter exclusion, native callback failures in both policies, active-owner moves,
immediate rearming, reentrant destruction during delivery/close, multiple stopped
close joiners, late delivery, affinity, closed/moved-from use, failed-start
retention, embedded-NUL rejection, and real native filesystem notifications.
The historical callback and layout tests now exercise `uv::raw::fs_event`,
including application-owned native `data` preservation.

Local Linux/libuv 1.51.0 validation passes all 15 new cases under GCC and Clang.
`make test-all GTEST_ARGS=--gtest_brief=1` passes both complete 374-case suites
(372 passed, two existing TTY tests skipped), plus each separate UDP allocation
binary; examples build as part of that target. A focused Clang ASan/UBSan build
with leak checking passes fs-event, historical watcher, and layout tests (20
passed, one existing TTY skip). The three affected public headers also compile
independently under C++20. Network suites and LeakSanitizer were run outside the
sandbox to avoid its network/ptrace restrictions.

Cross-platform/backend validation, allocation fault injection for filename
materialization, allocation measurements, and broader performance gates remain
open; local coverage does not freeze the API or establish portable event semantics.
