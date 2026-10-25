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
remains the Unix fallback. The internal `uvpp-validation-options` interface
target carries MSVC warning/conformance options and `NOMINMAX` /
`WIN32_LEAN_AND_MEAN`. Each test and example executable explicitly links it
privately; the public `uvpp::uvpp` target and installed export do not depend on
it. This replaces directory-wide target enumeration. Thread-pool fixtures read
`UV_THREADPOOL_SIZE` through `uv_os_getenv`, retrying with a resized buffer on
`UV_ENOBUFS`; no CRT deprecation suppression is defined. Contributor commands and
dependencies are in
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
only that platform's baseline. Minimum-libuv coverage,
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

MSVC warning cleanup stays in the test fixtures: the wide-native-buffer case
uses a dependent native type so the Windows length assignment is not
instantiated; failing value-returning coroutines use `co_return` with a throwing
helper; joiner variables have distinct names; and the Unix backend-fd body is
excluded on Windows after the explicit skip. Compiler warnings remain enabled.

Buffer rejection tests compare size limits through dependent native length/count
types as well. This avoids Clang's tautological-comparison warning when a native
type aliases `size_t`, while retaining overflow rejection checks on platforms
with narrower native fields.

UDP batch copy coverage includes copy construction and assignment from both a
fresh empty lot and a cleared lot retaining capacity, replacing populated
assignment destinations, and reuse of the empty copies. A separate ordinary
copy test uses three datagrams with 2/3/2 buffers: it verifies the three-entry
view, seven buffer records, IPv4/IPv6/default destinations, and each borrowed
base/length after overwriting source metadata. Copies own distinct metadata
arrays and preserve borrowed addresses and payload pointers. The isolated
allocation sweep remains separate and covers rollback/reusability under failure.

Installed-package validation runs in every ordinary platform/compiler CI job.
It installs to the runner's temporary directory, moves that prefix, copies only
`examples/cmake-consumer` outside the checkout, and configures/builds it with the
same compiler (and vcpkg toolchain on Windows). The optional expected-prefix guard
requires an imported public target and verifies the package directory and
exported uvpp include paths are within the relocated installation. The consumer
sets its own Windows header macros; it never links repository validation targets.
The release-package workflow's external consumer build is retained.

`FindLibUV.cmake` prefers upstream `libuv::uv_a` over `libuv::uv` when both exist,
independent of `BUILD_SHARED_LIBS`. Its `LibUV::LibUV` interface wrapper retains
the selected target's transitive usage requirements. Older vcpkg config targets
and normal pkg-config/path/library discovery remain fallbacks; the latter does
not impose static linkage. The module is installed next to `uvppConfig.cmake` so
dependency discovery runs from the package, not a source-tree module path.

## MSVC filesystem compilation follow-up

A reported MSVC 14.44.35207 build stops with `C1001` at the end of
`test-fs.cpp`. The filesystem failure tests now use a namespace-scope callback
function instead of a local closure object as a non-type template argument, and
prepare the submission lambda outside `EXPECT_THROW` rather than embedding
nested lambdas inside that macro. Assertions still cover runtime/static native
submission rejection, cleanup/reuse, and high-level cleanup before throwing.
This is a targeted compiler-workaround candidate; the end-of-file diagnostic
alone does not identify the precise construct that caused the internal error.
A native MSVC rerun is required to confirm resolution.

The same log reports `C4702` for the raw timer conversion's floating fallback
following integral branches that always return. Compile-time integral and
floating/general-ratio paths are now mutually exclusive, preserving conversion
semantics without compiling an unreachable suffix. Existing conversion tests
also exercise positive and negative integral durations with a fractional unit
ratio. The Windows file fixture uses
[`_wsopen_s`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/sopen-s-wsopen-s)
with `_SH_DENYNO` in place of
deprecated `_wopen`, retaining binary I/O, permission masking, and the descriptor
or `-1`/`errno` outcome. Warning flags remain enabled; no CRT deprecation
suppression is added.

A subsequent reported MSVC run compiles the filesystem tests and timer example,
then rejects a local `next` constexpr callback referenced as a template argument
inside a generic timer lambda (`C2065` and cascading callability/GTest errors).
The one-shot rearming fixture now uses a namespace-scope static callback and
explicitly typed runtime callbacks. Its runtime-to-static-to-runtime sequence,
three deliveries, inactive one-shot observation, capture lifetime, and close
checks remain intact. Native Windows CI must confirm the lookup workaround.

The next reported Windows build reaches the typed phase-watcher rearming test,
then reports `C2326` for its local constexpr `replacement` target referenced from
inside another callback. All local static-dispatch targets in the phase fixtures
now use namespace-scope function-template specializations instead. Runtime/static
switching, reentrant replacement, capture lifetime, and closing-state assertions
remain, with no platform skip. For new cross-platform fixtures invoking static
callback APIs from another callback, prefer named namespace-scope function targets
to avoid the reported local-constant lookup paths in MSVC. Native CI must confirm
this typed-test follow-up as well.

## Raw stream persistent migration

`test-raw-stream.cpp` covers status guards, cross-loop/type accept rejection,
native would-block and blocking-mode outcomes, runtime/static listener and read
replacement, mutable/reentrant callable lifetime, terminal and nonterminal
notifications, paired buffer loans across replacement/stop, and all six normal/
reset close forms. The inactive-listener-error test injects delivery after native
quiescence; it does not reproduce a Windows AcceptEx failure. Real loopback TCP
reads and shutdown cover borrowed payload and EOF. Callback preparation exceptions
remain distinct from native failures, and callback exceptions terminate.

The isolated allocation binary (`udp-allocation.cpp`, also used for network-owner
allocation tests) prepares heap-backed stream callables before disabling C++
allocation. Starting, delivering 1,000 allocation/read callback pairs and stopping
must then succeed. This measures wrapper C++ allocation for prepared callbacks and
caller-provided storage; it does not count libuv backend C allocation or user buffer
allocation, nor claim callback construction is allocation-free.

Windows-target syntax checks use MinGW headers and named namespace-scope static
callback targets. Native Windows/MSVC execution remains a CI check; local Linux
runs and cross-compilation do not substitute for that backend validation.
