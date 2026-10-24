# Repository validation

The CI workflow selects Ubuntu 24.04 x64 (GCC and Clang), Windows Server 2022
x64 (MSVC), and macOS 26 ARM64 (Apple Clang). These jobs build the opt-in
`UVPP_BUILD_TESTS` and `UVPP_BUILD_EXAMPLES` CMake targets in Debug mode. CTest
runs the main suite and the separate UDP allocation-injection binary, each with
a 300-second timeout. Failed jobs retain CTest diagnostics as artifacts.
Google Test's per-case progress remains in CTest logs so an abort identifies the
last running case. Linux ASan/UBSan remains a separate Makefile job with leak detection.

The library interface target has no Google Test dependency. CMake prefers
libuv's config targets (including the older vcpkg unofficial target), retaining
configuration-specific paths and system libraries; pkg-config/path discovery
remains the Unix fallback. MSVC validation binaries enable conforming C++20
parsing. Contributor commands and dependencies are in
[repository checks](../user/getting-started.md#repository-checks).

Test portability lives in fixtures rather than a second library API. Child
process tests invoke the test executable in a dedicated mode before Google Test
initialization, exercising arguments, cwd, environment, exit, wait cancellation,
and cleanup without a shell. Windows pipes use the native named-pipe namespace;
Unix socket paths remain short enough for macOS. Coroutine polling uses borrowed
sockets on every platform, with POSIX-only descriptor checks guarded separately.
POSIX signal delivery and raw pipe-descriptor polling have explicit Windows skip
cases. Environment-dependent TTY and symlink skips remain visible.

Configured jobs do not by themselves establish platform support. Native runner
results are required for Windows/macOS validation; local Linux results establish
only that platform's baseline. Minimum-libuv coverage, installed-package CI,
broader fault injection, and performance gates remain tracked in
[proposal 010](../proposals/010-validation-and-performance.md).

Native filesystem notification fixtures resolve the existing temporary directory
before appending a test name. On Windows they additionally expand 8.3 aliases
with `GetLongPathNameW`, matching libuv's long-path prefix comparison. The native
coroutine watcher test produces repeated timer-driven mutations while waiting
for two deliveries, with a bounded deadline. It does not assume synchronous
backend arming or one notification per write; deterministic trampoline tests
continue to cover exact subscription/slot behavior.

The UDP batch copy constructor initializes its pointer-metadata vector with the
fallible size constructor before rebuilding pointers into copied storage. This
keeps allocation injection out of MSVC's debug vector default constructor, which
allocates an iterator proxy inside `noexcept`. Copy assignment retains its
copy/swap rollback guarantee, with no change to borrowed payload ownership or
release-build allocation count. The isolated failure-budget sweep covers those
metadata construction failures as well as copied buffer storage.

Interface-name lookup and IPv6 scope-identifier conversion have separate tests:
zero is invalid for name lookup, while Windows identifier conversion simply
formats the numeric value and Unix delegates to name lookup. Tests preserve
those native semantics. The CMake test property and workflow CLI both allow
300 seconds per binary; the workflow's 30-minute job limit remains separate.
A successful macOS retry after a timeout does not establish whether runner load
or an intermittent test caused the original delay.
