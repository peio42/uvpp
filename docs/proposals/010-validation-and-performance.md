# Lifecycle Validation and Performance Baselines

Status: partially implemented.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3 development on `main`.

This proposal is not an implemented API or a release commitment. Names are provisional.

## Motivation and current behavior

The repository runs GCC and Clang tests on Ubuntu and includes native-layout tests.
The CI workflow now configures Windows/MSVC x64 and macOS/Apple Clang ARM64
alongside Linux GCC/Clang, using opt-in CMake test and example targets. Passing
native runner results remain the validation gate; local Linux results alone do
not establish Windows or macOS support. There is no broad benchmark suite; the first
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

The platform workflow and CMake/CTest targets are implemented, including the
isolated UDP allocation binary, compiled examples, bounded test timeouts, and
failure-log artifacts. Test fixtures use native named-pipe paths on Windows,
sockets for coroutine polling, and the test executable as a child process for
portable argument/environment/cwd/exit validation. POSIX-only signal delivery
and raw pipe-descriptor polling remain explicit Windows skips. Platform runner
validation is still required; no minimum-libuv baseline is established here.

Local Linux/libuv 1.51.0 validation of this platform-CI slice passes
`make test-all GTEST_ARGS=--gtest_brief=1` under GCC and Clang: each main suite
runs 390 tests (388 pass, two TTY skips), and both isolated allocation cases pass.
CMake's Debug build compiles all examples and both test binaries; CTest passes
both executables. An installed-package consumer also configures and builds on
Linux. Integration tests run outside the network-restricted sandbox. Windows
and macOS jobs have not been executed locally; their native results remain pending.

Broader TSan CI work, fault injection beyond the existing UDP case,
and recorded cross-platform benchmark baselines remain proposed. Acceptance requires those additions plus verified incremental rebuilding,
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

## Platform-CI failure follow-up

The first native macOS run exposed an OS-dependent UDP failure fixture: sending
to port zero may succeed. The result-surface test now uses an `AF_UNSPEC` address
to exercise deterministic libuv submission validation. The Windows build exposed
positional `uv_buf_t` initialization in filesystem coroutine awaiters; both now
use named-field construction and length validation. Filesystem test paths use
`uv_os_getpid()` instead of the POSIX CRT alias.

Mutex and read/write lock contention probes now run on a different thread from
the lock owner, avoiding Windows recursive-mutex assumptions and BSD self-lock
errors. CTest preserves Google Test's per-case progress so a native abort reports
the last running case. This addresses a plausible macOS abort path; the original
brief log does not identify which test aborted. Passing native reruns are still
required before claiming either platform is validated.

Local follow-up validation passes `make test-all GTEST_ARGS=--gtest_brief=1`
under GCC and Clang: each main suite passes 388 of 390 cases with two TTY skips,
and each isolated allocation suite passes both cases. Filesystem test sources
also pass syntax-only GCC/Clang compilation with a temporary libuv header overlay
that puts a 32-bit `len` before `base`, matching the Windows buffer layout; this
is a focused structural check, not an MSVC build or Windows runtime validation.
CMake configuration confirms both CTest commands retain per-case progress.

## Native filesystem notifications and MSVC allocation injection

A subsequent native run reached the end of the macOS suite, with only the
filesystem subscription integration test failing (zero deliveries before its
two-second deadline). Windows reached the same test and asserted in libuv's
long-filename/parent-path comparison; the isolated allocation test also stopped
inside its UDP copy-assignment failure sweep.

The notification fixtures now canonicalize their temporary directory and expand
Windows short-name aliases before watcher startup. The coroutine integration
test drives repeated real mutations from a timer until two notifications arrive,
rather than relying on a single write made before `loop.run()`. A five-second
deadline still fails stalled delivery, and duplicate/coalesced notifications
remain valid. Precise subscription transitions retain deterministic callback
coverage.

For UDP copy assignment, the last vector member previously default-constructed
under allocation injection. MSVC's debug STL allocates a proxy in that noexcept
constructor; it now uses the fallible size constructor and rebuilds pointers
into the copied storage. The existing allocation sweep still visits every
failure boundary and verifies rollback/reusability. Both fixes require native
reruns; the Windows log does not provide a stack trace for the allocation abort.

Local Linux validation passes both complete GCC/Clang suites (389 passed,
two TTY skips per 391-case main suite; both isolated allocation cases pass).
Examples also build. The native coroutine notification case passes 100
consecutive repetitions with each compiler. These checks do not exercise
Windows short-path expansion, MSVC iterator proxies, or the macOS backend;
passing platform CI reruns remain required.

## Immediate IPC write result on Windows

The remaining reported pipe test expected a terminal error from an unconnected
immediate handle write. Windows `uv_try_write2()` returns `UV_EAGAIN` because
immediate handle transfer is unsupported; the wrapper already preserves that
outcome. The test now asserts the exact Windows would-block result (including
empty error code and zero progress), while retaining Unix terminal-error checks.
No production result mapping changes are needed. Native Windows CI must confirm
the adjusted test.

`make test-all GTEST_ARGS=--gtest_brief=1` passes locally with GCC and Clang:
389 main tests pass with two TTY skips per compiler, both isolated allocation
cases pass, and examples build.

## Interface identifier semantics and platform timeout

The reported Windows interface test conflated name lookup with IPv6 scope
identifier conversion. The wrapper is unchanged: Windows `uv_if_indextoiid()`
formats an index as a string, including zero; Unix delegates to interface-name
lookup. Separate tests now retain zero-index name failure on every platform and
assert the platform-specific identifier outcome.

A reported macOS run timed out at 180 seconds and passed on retry in approximately
63 seconds. Both the CMake per-test property and the workflow's CTest option now
allow 300 seconds per binary. The cause of the original timeout remains unknown;
this gives slower runs more margin without removing the deadline or per-case
progress diagnostics. The overall job timeout stays at 30 minutes.

Local GCC/Clang validation passes both complete 392-case main suites (390 pass,
two TTY skips), both allocation suites, and example builds. CMake's generated
CTest properties confirm 300-second limits for both binaries. Native CI reruns
remain necessary for the Windows-specific assertion and macOS timeout margin.

## MSVC test warning cleanup

After the reported platform suites passed, Windows compilation still warned
about a wide buffer assignment in an inactive non-template branch, value-returning
coroutines ending in unconditional throws, a shadowed joiner name, and statements
after the Windows backend-fd skip. Test fixtures now make the buffer type
dependent, express the three intentional coroutine failures through `co_return`
and a throwing helper, rename the joiner variable, and compile the Unix-only
backend-fd body under `#else`. No library behavior or warning flags change.
Native MSVC compilation must confirm disappearance of the reported diagnostics.

Local `make test-all GTEST_ARGS=--gtest_brief=1` passes with GCC and Clang:
390 main tests pass with two TTY skips, both allocation cases pass, and examples
build. The buffer test also passes a Clang syntax-only check with 32-bit,
Windows-order native buffer fields and constant/64-to-32 conversion diagnostics
promoted to errors. This synthetic check does not replace an MSVC build.

The remaining Clang self-comparison warning in buffer-limit rejection tests is
addressed by making both native length and count types template parameters.
On Unix, native lengths can alias `size_t`; on Windows the narrower native
length still exercises rejection. Test expectations and warning flags are
unchanged.

Both complete GCC/Clang suites pass locally (390 main tests passed, two TTY
skips per compiler, plus both allocation cases and example builds). Focused
Clang syntax checks pass with `-Werror=tautological-compare` for the native Unix
layout and a simulated Windows 32-bit length layout; the latter also promotes
constant and 64-to-32 conversion warnings to errors.

## UDP batch copy invariants

The allocation sweep already copies 20 datagrams with two buffers each and
checks rollback, source/destination metadata separation, and successful reuse.
Ordinary copy-assignment coverage previously used two datagrams with 2/1 buffers.
New ordinary tests explicitly cover copy construction and assignment of fresh
and cleared empty lots, including replacement of a populated destination and
reuse, plus three datagrams with 2/3/2 buffers. The latter asserts that the view
count equals the three datagrams while the total buffer count is seven, checks
each native base/length and IPv4/IPv6/default destination, and verifies that
copies survive source metadata overwrite with distinct owned metadata arrays.
Payloads and addresses remain borrowed. Production copy logic is unchanged.

Local `make test-all GTEST_ARGS=--gtest_brief=1` passes with GCC and Clang:
each main suite runs 394 cases (392 passed, two TTY skips), both isolated
allocation cases pass, and examples build. The new pure-metadata cases remain
under `UVPP_HAS_UDP_TRY_SEND2`, matching the batch API's availability gate.

## Explicit MSVC validation options

MSVC repository flags now live in the internal `uvpp-validation-options`
interface target. The two test executables and each example explicitly link it
privately, replacing the directory target scan. The target carries `/W4`,
`/permissive-`, `/Zc:__cplusplus`, `/utf-8`, `NOMINMAX`, and
`WIN32_LEAN_AND_MEAN`. The thread-pool fixture now uses `uv_os_getenv`, including
buffer resizing on `UV_ENOBUFS`, so CRT deprecation suppression is removed.
Missing or empty environment values retain the default, and the existing parsed
range of 1–32 workers is unchanged. Other native environment errors surface as
errors rather than being misreported as an out-of-range test skip.
The internal target is not installed/exported or linked through the public
library interface.
Native MSVC CI remains responsible for compiler-specific build validation.

Local validation passes CMake's Debug build and both CTest binaries, complete
Make GCC/Clang suites (392 main cases passed, two TTY skips per compiler, both
allocation cases passed), and example builds. A configuration-only MSVC-branch
check on Linux confirms generated test/example flags; this is not an MSVC
compilation. The installed public export contains
none of these options/definitions, and its external consumer configures/builds.

The native-environment follow-up passes complete GCC/Clang suites and both CTest
binaries. Fresh-process cancellation checks pass with two workers and with a
long zero-padded value representing two workers, exercising the `UV_ENOBUFS`
resize path. Configuration-only MSVC-branch generation confirms the CRT
suppression is absent from all test/example compilation commands.

## Installed-package and libuv target discovery

The static-first upstream config-target order (`libuv::uv_a`, then `libuv::uv`)
is now documented, including its independence from `BUILD_SHARED_LIBS` and the
separate fallback library-search behavior. `LibUV::LibUV` still wraps the selected
target and retains its transitive/platform/configuration requirements.

Ordinary platform CI jobs now install and relocate uvpp outside the checkout and
configure/build a copied external consumer with `find_package(uvpp CONFIG REQUIRED)`.
The consumer checks the imported target and package/header paths against the
relocated prefix. The installed config discovers libuv through the installed
find module; no repository validation target is required. Windows applications
set their own header macros privately. The existing release-package consumer
check remains in place; passing native platform CI is still required.

Local validation installs to a temporary prefix, relocates it, and builds the
copied external consumer with GCC and Clang; an incorrect expected prefix is
rejected. A controlled libuv config exposing both real system static/shared
libraries selects `libuv::uv_a` and links successfully through the interface
wrapper even with `BUILD_SHARED_LIBS=ON`. Complete Make GCC/Clang suites also
pass (392 main cases passed, two TTY skips per compiler, both allocation cases
passed, examples built). Native Windows/macOS execution of the new installed
consumer steps remains a CI validation gate.

## Raw filesystem migration validation

The `uv::raw::fs` migration retains existing I/O, metadata, path, directory,
static-callback, and request-layout coverage. Additional filesystem tests exercise
immediate native rejection in runtime/static read submissions, callback rollback,
explicit cleanup and reuse, stable native address and application-owned `data`,
delayed completion errors, directory ownership on rejected close submission, and
the high-level submission adapter's cleanup/throwing behavior.

Local Linux validation passes both complete GCC and Clang suites: 390 passed,
two existing TTY skips per compiler, plus both separate UDP allocation tests and
example builds. A focused Clang ASan/UBSan run passes all 41 filesystem callback,
path, and coroutine cases with leak detection. The four affected public headers
compile independently under C++20 with both compilers. Network tests and leak
checking require execution outside the sandbox because of socket/ptrace limits.
These results do not establish cross-platform behavior or allocation/performance
budgets; the broader gates above remain open.

## Raw signal/poll/fs-event migration validation

[Raw watcher tests](../../tests/test-raw-watchers.cpp) cover normalized event
statuses, fixed native identity and application `data`, rejected runtime/static
starts, callback capture release, native poll start conflict and retry on Linux,
zero/invalid poll masks, injected terminal callback errors, runtime/static
rearming after terminal delivery, real signal one-shot and persistent rearming,
fs-event path results and embedded NUL rejection, explicit common native queries,
and capture release before native close completion. Existing native signal,
readiness, fs-event, watcher-layout, and high-level source lifecycle tests remain.

Local Linux validation passes both complete GCC and Clang suites: 403 passed and
two existing TTY skips per compiler, plus both separate UDP allocation tests;
examples build successfully. A focused Clang ASan/UBSan binary passes 67 tests
covering raw watchers, historical callback/process coverage, fs-poll, and all
three high-level watcher source families, with leak detection enabled. The three
raw watcher public headers compile independently with both compilers under C++20.
The complete suites and leak checking run outside the sandbox because of socket
and ptrace restrictions. Cross-platform validation, allocation fault injection,
and measured allocation/performance budgets remain open.

The raw-watcher test source now guards POSIX headers, socket fixtures, and
signal-delivery cases on Windows, reporting explicit skips for the latter two.
Signal registration/lifetime checks use `SIGINT`, and temporary-directory setup
uses only standard C++ with exclusive directory creation instead of `getpid()`.
Generic status, signal registration/close, native-query, and fs-event setup tests
remain available. This removes those source-level POSIX dependencies; it does
not constitute a native Windows build or runtime validation.

## Raw timer/idle/prepare/check migration validation

[Raw loop-watcher tests](../../tests/test-raw-loop-watchers.cpp) add 27 cases:
three typed phase-watcher suites cover persistent mutable state, stable native
identity and application `data`, active runtime/static replacement in both
directions, reentrant replacement, stop/reuse, capture release, native close
completion, and rejected operations on closing handles. Timer cases cover
terminal one-shot release without explicit stop, runtime/static rearming,
repeat-to-zero final delivery, active-only `again`, stop/reuse, closing-state
errors, invalid-duration rollback, finite-negative/sub-millisecond conversion,
full-width unsigned repeat counts, and owner release from native close completion.
The fixtures use only standard C++ and libuv; no POSIX-only headers, native
socket fixtures, or signal delivery are required. Phase delivery uses bounded
`run(nowait)` iterations instead of relying on a prepare/check watcher to wake
an otherwise blocking poll.

Local Linux validation passes both complete GCC/Clang suites: 434 passed,
two existing TTY skips per compiler, both isolated allocation tests passed, and
examples built. A focused Clang ASan/UBSan binary passes all 43 raw loop-watcher,
historical timer/phase, and async cases with leak detection. All four migrated
public headers compile independently with both compilers under C++20 and
`-Wall -Wextra -Werror`. Complete network suites and leak checking run outside
the sandbox because of socket/ptrace restrictions. Native Windows/macOS jobs,
allocation-failure injection, and measured performance budgets remain validation
gates; the portable fixtures are not a claim of native platform execution.

Reentrant start rejection now has two additional regression cases. A repeating
raw timer rejects an invalid repeat duration from inside its executing callback,
then delivers the same mutable callable again on its unchanged native schedule.
A raw signal callback attempts `start(0, ...)`, reaching libuv's native rejection
and `commit_start` while `persistent_callback_slot` is temporarily empty during
delivery. Two explicit invocations of the registered native dispatch verify that
the original callable is restored with its mutable state intact, without relying
on platform-specific OS signal delivery. Both cases check rejected-capture release,
continued original-capture lifetime, native activity, and capture release at stop.

## Reported MSVC raw-migration build failure

The reported Windows build fails with MSVC 14.44.35207 `C1001` at the end of
`test-fs.cpp`, before test execution. The failure fixtures have been simplified:
namespace-scope function callbacks replace the closure-object template argument,
and `EXPECT_THROW` receives a prebuilt submission callable. Both native rejection
and high-level cleanup assertions remain. This addresses plausible compiler
triggers without establishing which construct caused the internal compiler error;
passing native CI remains required.

The accompanying timer `C4702` warnings are addressed by making integral and
floating/general-ratio conversion branches compile-time alternatives. The
fractional-ratio integral branch has additional positive/negative expectations.
The deprecated `_wopen` test fixture is replaced by `_wsopen_s`, preserving its
binary/sharing/permission/error behavior without disabling warnings. See
[repository validation](../design/validation.md#msvc-filesystem-compilation-follow-up).

Local Linux validation passes both complete GCC/Clang suites (436 main cases
passed and two existing TTY skips per compiler), both isolated allocation cases,
and example builds. A focused Clang syntax check instantiates integral,
floating-point, and fractional-ratio timer conversions with unreachable-code
diagnostics promoted to errors. These checks do not reproduce MSVC's frontend
or execute the Windows CRT branch; a native CI rerun must confirm both the
internal-error workaround and the reported warning cleanup.

The next reported MSVC run advances past the filesystem compilation, then fails
in the one-shot timer rearming fixture: a generic lambda cannot resolve the
local constexpr `next` used as a template argument (`C2065`, followed by cascading
callability/GTest diagnostics). The fixture now uses a namespace-scope function
and concrete timer-reference callback parameters, retaining all rearming and
capture-lifetime assertions. This changes test expression shape only; a native
CI rerun remains required to validate the MSVC workaround.

The rearming-fixture follow-up passes complete GCC/Clang suites locally: 436 main
cases pass with two existing TTY skips per compiler, both isolated allocation
cases pass, and examples build. No library API, test skip, or warning policy changes.

A further reported MSVC run reveals the corresponding local-target lookup failure
in the typed phase tests (`C2326`, inaccessible `replacement`). Their static
callback targets now all use namespace-scope function-template specializations,
including direct runtime/static replacements and closing-state rejection. The
original three watcher types and all lifecycle/capture assertions remain covered,
without additional skips. This fixture-only follow-up still requires native
Windows confirmation.

The typed-phase follow-up passes all 18 focused phase cases and both complete
GCC/Clang suites locally (436 main cases passed, two existing TTY skips per
compiler, both isolated allocation cases passed, examples built).

## Raw async migration validation

[Raw async tests](../../tests/test-raw-async.cpp) add 21 portable cases covering
active/default construction, all loop/loop-view runtime/static constructors,
fixed native identity and application-owned `data`, burst coalescing, mutable
runtime callbacks, pending notification selection, runtime/static replacement in
both directions, reentrant replacement and clearing, capture release, setter
rejection while closing, native queries, ref/unref liveness, and owner release
from static native close completion. Callable-copy exceptions exercise preparation
before native initialization, preservation of a pending static target, and
restoration of an executing runtime slot after rejected preparation. Runtime and
static callback exceptions are tested at the terminating C boundary.

A worker notification checks loop-thread delivery and joins the producer before
close. Four concurrent producers publish 1,000 items through an application-owned
mutex/queue while the loop drains notifications, then join before final drain and
close. Tests do not equate send count with callback count, race send against close,
read non-atomic closing flags from producers, or depend on a native memory-order
guarantee newer than the linked libuv. All fixture primitives are standard C++20
and libuv; static template targets are namespace-scope functions to avoid the
reported MSVC local-target lookup failures. Multi-handle startup and close tests
join actual delivery/completion without requiring a single backend poll to suffice.

Local Linux/libuv 1.51.0 validation passes complete GCC/Clang suites: 457 passed,
two existing TTY skips per compiler, both isolated allocation cases passed, and
examples built. A focused Clang ASan/UBSan binary passes 32 tests with one existing
TTY layout skip, including raw async, historical simple handles, and layout,
with leak detection enabled. The async public header compiles independently under
C++20 with both compilers and `-Wall -Wextra -Werror`. Complete suites and leak
checking run outside the sandbox because of socket/ptrace restrictions.

Native Windows/macOS CI, deterministic native init/send failure injection,
allocator fault injection beyond throwing callable preparation, async-specific
TSan coverage, and measured allocation/performance budgets remain gates. These
local results do not establish native platform execution or race-safe send/close
admission; the raw lifetime precondition is explicit in
[the user contract](../user/async.md).

## Raw timer explicit initialization validation

[`test-raw-timer-init.cpp`](../../tests/test-raw-timer-init.cpp) adds seven portable
cases for default construction without loop registration, rejection and callable
release before init, inert pre-init runtime/static close, stable native storage
and application data across init, repeated init preserving an active subscription
and its original loop, initialization remaining observable through close completion,
and the existing loop/view constructors sharing the same initialized contract.
Namespace-scope static callback targets preserve the established MSVC test pattern.

The 16 focused initialization/timer cases pass locally. Full GCC and Clang
`make test-all` runs each pass 464 tests with two existing TTY skips, plus both
separate UDP allocation cases and example builds. The 35 raw timer/phase cases
also pass under ASan/UBSan with leak detection. Native timer initialization
currently has no failing path on a valid loop, so this coverage does not simulate
native init failures or justify extending rollback assumptions to other families.
Native Windows/macOS execution remains a CI gate.

## Raw loop-phase explicit initialization validation

[`test-raw-phase-init.cpp`](../../tests/test-raw-phase-init.cpp) adds seven typed
cases per idle/prepare/check handle (21 cases) covering no native registration
before init, status errors/capture release on pre-init operations, inert close
variants, stable native identity/application data, duplicate init preserving the
active subscription and original loop, close-completion observations, and the
throwing loop/view constructors. Phase delivery tests use nonblocking loop
iterations rather than assuming active prepare/check wakes a blocking poll.
Static callback targets are namespace-scope function templates for MSVC portability.

The timer lifecycle guards are extracted into a shared internal helper without
new data members or changes to the native reconstruction/callback-slot protocol.
Existing timer lifecycle, subscription, and native layout cases are included in
regression validation. The targeted ASan/UBSan run with leak detection passes 58
cases, with one existing TTY skip. Native Unix and Windows phase init implementations
currently have no failing path on a valid loop; these tests do not simulate init
failure. Native Windows/macOS execution remains a CI gate.

Full local `make test-all` validation passes with GCC and Clang: 485 tests pass
per compiler with two existing TTY skips, both separate UDP allocation cases pass,
and all examples build. The new typed tests and timer initialization tests also
compile with Clang `-Wall -Wextra -Werror`. No native Windows/macOS execution was
performed locally.

## Raw signal/fs-event explicit initialization validation

[`test-raw-subscription-init.cpp`](../../tests/test-raw-subscription-init.cpp)
adds eight typed lifecycle cases per signal/fs-event handle and one reentrant
signal one-shot case (17 cases). Coverage includes no native registration before
init, all start/stop and native-query pre-init rejections, fs-event path/flag
variants, callback capture release, inert close variants, stable native identity
and application data, repeated init preserving an active subscription/path/loop,
initialized observations through close completion, throwing loop/view constructors,
and recovery from failed starts after successful initialization. The one-shot
case rejects init inside delivery and rearms another terminal subscription.

The tests use C++ filesystem facilities and portable registered native callback
dispatch rather than platform-specific signal delivery or event timing. Static
targets are namespace-scope function templates/functions for MSVC portability.
The common helper now forwards notification argument types without changing slot
storage or adding initialization state. Regression validation includes existing
watcher, timer/phase, native-layout, and initialization cases.

The targeted ASan/UBSan run with leak detection passes 89 tests with one existing
TTY skip. The new tests compile under Clang `-Wall -Wextra -Werror`. Native signal
init failure injection remains open: the verified Unix failure path precedes handle
registration, while ordinary initialized loops may already own the needed signal
infrastructure. Native Windows/macOS execution remains a CI gate.

Full local `make test-all` validation passes with GCC and Clang: 502 tests pass
per compiler with two existing TTY skips, both separate UDP allocation cases pass,
and all examples build. The final incremental run after bracing GoogleTest
assertions in constexpr branches emits no warnings; the new tests also pass GCC
`-Wall -Wextra -Werror` syntax validation.

## Raw poll explicit initialization validation

[`test-raw-poll-init.cpp`](../../tests/test-raw-poll-init.cpp) adds eight lifecycle
cases parameterized over descriptor and tagged socket init (16 local Unix cases;
Windows runs the native-socket mode). Coverage includes default storage without
loop registration, pre-init operational/native-query errors and callable release,
inert close variants, native failure/retry, stable native identity/application data,
duplicate init preserving the active subscription/descriptor/original loop,
reentrant duplicate init, close completion and borrowed socket ownership, throwing
constructor failure cleanup, actual writable readiness with a bounded nonblocking
loop, and late-init registration rollback between neighboring handles.

The late-failure case creates a genuine inactive native poll registration, injects
an init error into the rollback boundary before any request starts, verifies loop
walk/liveness and native-data preservation, then retries and closes normally.
It runs across platforms without needing to force Windows `getsockopt` to fail.
It validates registration cleanup, not a claim of native Winsock fault injection.
The rollback has an isolated libuv-internal queue dependency (historical array and
current structure forms); review that boundary when the backend changes.

The 16 focused cases pass, as does Clang `-Wall -Wextra -Werror` compilation.
Full `make test-all` passes 518 tests per GCC/Clang compiler with two existing TTY
skips, both separate UDP allocation cases, and all example builds. Targeted
ASan/UBSan with leak detection passes 49 tests with one existing TTY skip. Native
Windows/macOS execution and a native Windows post-registration fault injection
remain CI/follow-up validation gates. Positive integer-descriptor initialization
is covered on Unix; Windows tests use native sockets and cover negative descriptor
validation rather than manufacturing CRT ownership of a socket.

## Raw async explicit initialization validation

[`test-raw-async-init.cpp`](../../tests/test-raw-async-init.cpp) adds eleven cases
covering no native registration before init, pre-init send/setter/query rejection
and capture release, inert close variants, stable native storage and application
data, immediate active/referenced state, all six loop/view and empty/runtime/static
init overloads, pending notification preservation across duplicate init, rejected
runtime/static selection changes, reentrant duplicate init retaining the mutable
runtime callable, initialized observations through close completion, callable
preparation failures before/after init, successful init publication to a producer
thread, and static native close completion.

The producer is joined before loop delivery can close the handle. No test races
init with send or sends during/after close: these remain protocol violations.
`send()` checks the native loop pointer only; its immutability after successful
init and application synchronization before publication are required, rather than
an atomic wrapper state or a closing check. Existing producer/callback replacement
and native layout tests remain regression coverage. Native-init fault injection,
native Windows/macOS execution, and async-specific TSan remain separate gates.

Targeted ASan/UBSan with leak detection passes 62 tests with one existing TTY skip,
including the existing async, timer/phase initialization, and native-layout cases.
The new tests compile with Clang `-Wall -Wextra -Werror`.

Full local `make test-all` passes 529 tests with GCC and 529 with Clang, with two
existing TTY skips per compiler, both separate UDP allocation cases passing, and
all examples built. No native Windows/macOS execution was performed locally.

## Native Windows poll late-failure validation

The previously open native post-registration fault path now has two Windows
cases in [`test-raw-poll-windows.cpp`](../../tests/test-raw-poll-windows.cpp).
They patch only the real native module's `getsockopt` import, observe the poll
registration through `uv_walk` inside its `SO_PROTOCOL_INFOW` call, return a
Winsock error, and let real libuv propagate that failure to the wrapper. They
verify status init rollback/retry and both loop-taking throwing constructors.
This is a real native-init execution with Winsock-call fault injection, rather
than the portable test's synthetic failure at the rollback boundary.

[`raw-poll-init-rollback.md`](../design/raw-poll-init-rollback.md) records the exact
thirteen official source tags audited, backend scope and assumptions, API
alternatives, and the transition from pointer-array queues to named queues in
1.46.0. The Windows CI workflow now includes 1.45.0/static and 1.53.0/shared native
jobs using a standalone validation project; the normal installed-libuv Windows
suite also contains the two cases. Failures to install the fault injector fail
the tests instead of skipping them. XML records runtime/header versions.

Local validation: both Windows header sets (1.45.0 and 1.53.0) pass Clang/MinGW
cross-target syntax checks with `-Wall -Wextra -Werror`. Linux binaries linked
against each of those actual source-built libuv versions pass all 16 poll cases,
including the registration-boundary rollback and retry test, covering both native
queue representations. Full GCC/Clang suites each pass 529 tests with two existing
TTY skips, both separate UDP allocation cases, and example builds. Workflow YAML
parses successfully. These are not native Windows/MSVC executions: the new native
jobs still require CI confirmation. The 1.44.2 tag is source-audited only because
the current loop wrapper requires metrics APIs introduced in 1.45.0.

## Common result explicit error check validation

The core result tests add three cases for `throw_if_error() const -> void` on
both result forms. They verify const/lvalue/rvalue checks do not consume move-only
values, in-place immovable values need no move/copy, nonzero error-code success
payloads are not mistaken for result errors, void success remains unchanged, and
failure preserves the native code/category and agrees with `value()`. Compile-time
checks retain all four typed `value()` reference categories.

Migrated raw constructors and other error-only throwing facades/examples now use
`throw_if_error()`; payload extraction keeps `value()`. Existing lifecycle and
await-policy tests provide regression coverage. The 34 focused core/timer/async
initialization cases pass under ASan/UBSan with leak detection, and core tests
compile under Clang `-Wall -Wextra -Werror`.

Full local `make test-all` passes 532 tests per GCC/Clang compiler with two existing
TTY skips, both separate UDP allocation cases passing, and all example builds.
The updated posting and timer examples also build with both compilers after their
error-only checks are switched to the new spelling. Native Windows/macOS runs
remain CI validation.

## Raw TCP first-slice validation

[`test-raw-tcp.cpp`](../../tests/test-raw-tcp.cpp) adds 16 lifecycle/operation cases
covering default/pre-init guards, inert close and capture release, stable storage
and application data, typed address/descriptor/buffer results, IPv4/IPv6 bind,
native socket ownership transfer by open, typed/range-checked keepalive seconds,
duplicate init preserving socket/loop, invalid-family failure and retry, normal
close subscription release, and ordinary/reset native close completion. Runtime
callback preparation failure is checked separately from native operational errors.

Connected cases retain separate connect/shutdown requests and verify their
identity/completion callback lifetimes. In particular a reset rejected during
pending shutdown is attempted from a mutable read callback through the registered
stream trampoline twice; its callable state and the shutdown request callback
survive until native completion. Successful reset releases read captures before
close completion while retaining its close callable through completion.

A Unix/macOS subprocess lowers only its soft descriptor limit after loop setup,
forcing real native socket creation failure inside init_ex after registration.
It restores the limit, checks no registration remains, verifies application data
and null initialization sentinel, propagates the stored error after restoring
the limit, and retries. Throwing constructor failure is separately covered with
an invalid family.
Windows compiles the portable lifecycle/operation cases without POSIX headers;
this subprocess case is conditionally excluded. Windows native init fault
injection remains separate validation work. Libuv init_ex cleanup was source-
reviewed for Unix/Windows at 1.45.0, 1.51.0, and 1.53.0; uvpp does not unlink
TCP's already-removed registration a second time.

Full local GCC/Clang `make test-all` each passes 548 tests with two existing TTY
skips, both separate UDP allocation cases, and all examples. Focused ASan/UBSan
with leak detection passes 28 tests with one existing TTY skip, including legacy
TCP/stream interoperability, native layout and native-query cases. The migrated
resource-scope cleanup benchmark builds; no new performance/allocation claim is
inferred from compilation. Clang/MinGW cross-target syntax checks cover the new
TCP cases and existing TCP/layout units with the repository's Windows defines
(the existing MinGW UV_ONCE_INIT missing-braces warning is suppressed for umbrella
header units). Native Windows/macOS execution remains a CI gate.

### TCP Windows option transitions and sanitizer-safe resource exhaustion

The options test now checks Windows' native `UV_ENOTSUP` when re-enabling
simultaneous accepts after disabling them; the wrapper preserves that native
failure. Unix retains its successful/no-op behavior.

The native socket-exhaustion subprocess restricts descriptors only across the
status-returning init call. It restores the soft limit before constructing or
inspecting C++ exceptions: UBSan vptr checking and symbolization may need to open
process metadata, and a zero descriptor limit can otherwise produce misleading
invalid-vptr diagnostics and sanitizer-internal failures. The native init still
fails after registration and must leave retryable unregistered storage. GoogleTest
uses its threadsafe re-exec style for this case, avoiding allocation/exception
work in a fork-only child after other suites have started native threads.

Validation of this correction: full GCC/Clang suites and the complete
`make test-asan-ubsan` target each pass 548 tests with two existing TTY skips;
the separate UDP allocation cases and examples also pass/build. The Windows
branch passes Clang/MinGW warnings-as-errors syntax validation. A second
ASan/UBSan runtime (Clang 16 with GCC 12 standard headers) reproduces the exact
old invalid-vptr / pthread_getattr_np / EMFILE symbolizer failure with the original
test, while the corrected subprocess test passes. Native Windows execution
remains to be confirmed by CI.

The explicit keepalive API (`keep_alive(seconds)` / `disable_keep_alive()`) reuses
these lifecycle/option cases, covering both methods before init and during close,
positive enable/disable, and zero/negative/overflow delay rejection. Full GCC/Clang
suites each pass 548 tests with two existing TTY skips, UDP allocation cases and
example builds; the 16 targeted TCP cases pass under ASan/UBSan with leak detection.

## CI portability follow-up

The shared test executable main now defaults death tests to Google Test's
threadsafe re-execution mode, with explicit command-line overrides still honored.
Raw stream close parameterization keeps conditional directives outside macro
arguments for traditional MSVC preprocessing. Linux abstract pipe names have
separate coverage for the initial namespace marker (bind2/connect2, 1.46+) and
additional NUL bytes (1.51+); older native versions explicitly exercise rejection.
The capability macro records the latter native version boundary rather than
raising the minimum libuv requirement. See
[implemented validation](../design/validation.md#ci-portability-follow-up-death-tests-abstract-names-and-msvc).

## Raw UDP I/O validation

[Native UDP I/O tests](../../tests/test-raw-udp-io.cpp) cover runtime/static
subscription replacement, stop/close with allocator-owned storage in use, native
ENOBUFS and rearming, empty/truncated datagrams, pending-request rejection with a
native-storage snapshot, native submission/completion failures, request reuse and
borrowed payload retention through close. Real recvmmsg tests retain the original
pair through notifications actually delivered after stop, replacement or close,
without requiring FREE or a fixed kernel batch size. Native callback state after
stop is compared against direct libuv, and loans without FREE are retained until
next allocation/close completion. The former callback-field workaround is removed;
uvpp leaves native chunk/FREE delivery to the linked backend/version. Immediate tests exercise native queue would-block, metadata
validation and partial prefix/suffix retry (allowing backend whole-call rejection).

[Isolated allocation coverage](../../tests/udp-allocation.cpp) adds prepared
heap-backed UDP callback submission and real native receive/send delivery, plus
a prepared immediate batch, with C++ allocation disabled. This measures the
wrapper path; it does not promise that libuv performs no native allocations.

GCC and Clang full suites pass 637 tests each with two existing TTY skips; each
separate allocation binary passes six cases, and all examples build. Focused
ASan/UBSan covers 48 UDP tests; a libuv 1.48 build/run passes 29 raw UDP tests,
with later batch APIs absent at compile time. Windows-target syntax checks pass
for UDP I/O tests, allocation tests and the example. Native Windows/macOS runtime
validation remains delegated to the existing CI; local checks do not claim it.

## Raw TTY progress

The [TTY tests](../../tests/test-tty.cpp) use an actual Unix pseudo-terminal and
Windows console handles rather than relying on stdout being interactive. They
cover lifecycle, failure/retry, mode/size, real stream I/O and native descriptor
ownership. A macOS-specific child forces late kqueue failure after native
registration and checks retry without a stale loop handle. Windows/macOS runtime
coverage remains a native CI gate. See [implemented TTY](../user/tty.md).
