# Errors, Results, and Operation Surfaces

Status: partially implemented.

Architecture: [000 — V3 architecture](000-v3-architecture.md).

Target: v3. The common vocabulary below is implemented; operation adaptation
remains incremental.

This proposal incorporates the former v3 naming notes and extends them to
composable operation results. It is not an implementation or a release commitment.

## Structural Error Surfaces

Existing low-level immediate-throwing and error-policy `try_*` conventions remain
migration work. V3 changes the structure under [000](000-v3-architecture.md):

```cpp
// Proposed convention, not implemented signatures.
uv::foo(...);       // ergonomic / throwing
uv::ops::foo(...);  // explicit operation / status-oriented
```

Domain subdivisions such as `uv::ops::fs` follow the same rule. `uv::ops` operates
on the same high-level owners and does not require caller-owned native requests.
It is not a fourth layer. Raw operations in `uv::raw` expose native operational
failures through explicit status/results; construction and storage failures need
separate contracts.

Do not multiply `_status`, `_ec`, `try_`, or `async_` spellings solely to select
error policy. Borrowing is the default write/send payload policy: use `write` and
`send`, with explicit semantic names such as `write_copy` or owning types for
copying and transfer. Keep immediate `*_now` operations. In v3, reserve `try_*`
for actual attempt-style operations such as `try_lock()`.

Throwing immediate conveniences unwrap explicit operational results. For awaited
operations, the ergonomic facade raises operational failures at the await, while
`uv::ops` delivers typed results. Callback conveniences may throw at submission;
asynchronous completion is always delivered as a result. Neither policy can throw
back into a caller whose submission call has already returned.

Both surfaces use the shared protocol in [004](004-operation-state.md), preserving
the same ownership, native effects, cleanup, and completion guarantees.

The selected public owner-close API follows this split:

```cpp
co_await owner.close();
co_await uv::ops::close(owner);
```

The member await throws an operational `UV_EBADF` or `UV_EBUSY`; the `uv::ops`
operation reports the same conditions in its explicit result. Loop-affinity
violations remain programmer misuse outside that result channel, and allocation
while registering a joining close waiter may still throw. Native handle close has
no completion status to adapt. The operation remains non-cancellable once it has
started; see [002](002-async-ownership.md) for its state and lifetime contract.

The process owner follows the same pairing for `wait()` and close:
`co_await child.wait()` throws `UV_EBADF`, `UV_EBUSY`, or `UV_ECANCELED`, while
`co_await uv::ops::wait(child)` returns `result<process_exit>`. A nonzero exit
status or terminating signal is payload, not an operation error. `kill(signum)`
throws immediate native failure and `uv::ops::kill(child, signum)` returns
`uv::status`.

## Unified Operation Results

V3 uses `uv::result<T>` and `uv::result<void>` as its common value/error
vocabulary. A result contains either one `T` or one `uv::error_code`; it never
exposes a value for an error result. `result<void>` represents success or an
operational error without a payload. `uv::status` is an alias for
`uv::result<void>` for APIs and local variables whose only outcome is that
status.

`uv::status` is the semantic alias for `uv::result<void>`. Public APIs should
prefer `status` when the operation carries no success payload; `result<void>`
remains the generic underlying type.

```cpp
uv::result<std::unique_ptr<connection>> connected{std::move(connection)};
uv::status closed;
uv::status refused = uv::status::from_native(UV_ECONNREFUSED);
```

`result<void>` accepts an `error_code`. Adapt a native libuv status through
`status::from_native(status)`, which makes that conversion explicit.

`result<T>` supports move-only values. It is copyable or movable exactly when
`T` permits the corresponding operation; it does not allocate. The primary
observation API is:

```cpp
if (result) {
  auto& value = result.value();
} else {
  uv::error_code code = result.error();
}
```

`has_value()` is equivalent to the explicit boolean conversion. `value()` has
`&`, `const &`, `&&`, and `const &&` overloads for value results. It throws
`uv::error` with the stored code when called on an error result; the `void`
specialization does the same. `error()` is non-throwing and returns an empty
`uv::error_code` for success. There are no `ok()`, `status()`, `canceled()`, or
`error_code()` synonyms on the common result type.

`uv::error_code` is a small libuv-specific value. `native()` exposes its
normalized native integer (zero or a negative libuv error), and `name()` and
`message()` expose libuv diagnostics; conversion to `std::error_code` is explicit.
`make_error_code(status)` normalizes all
non-negative statuses to an empty error code. The generic result uses this type
rather than a naked integer or an open-ended standard error category.

Specialized results remain necessary where the native completion has more state
than success-or-error. Reads retain EOF, byte count, partial progress, and their
borrowed buffer lifetime; immediate I/O retains would-block; datagrams, DNS, and
filesystem retain their typed payloads. Such types may expose a
`result<void>` status or eventually appear as `result<domain_outcome>`; this
milestone does not mechanically flatten them.

Explicit operations must cover native submission and completion failures through
the same result channel. Prefer deferred initiation or policy-aware construction
so setup does not submit and throw before the explicit policy takes effect.
An internal result adapter may implement the ergonomic facade, but a public
`as_result` spelling is not a second required error-selection API alongside
`uv::ops`. Exact callback and awaitable entry signatures remain to be prototyped.

Fallible synchronous high-level owner construction has a paired `uv::ops::make_*`
factory. The first implemented slice is TCP/pipe listeners and UDP sockets:
constructors throw native operational errors, while `make_tcp_listener`,
`make_pipe_listener`, and `make_udp_socket` return `result<Owner>`. Both policies
share result-oriented state creation and the same ownership/close protocol.
Native failure after init schedules close before error delivery, retains storage
through the callback, and requires the caller to keep driving the loop. C++
allocation/preparation failures still throw; these factories are not `noexcept`.
Async owner production retains operation names (connect/open/accept); factories
for other synchronous owners remain incremental work rather than an implemented
blanket surface.

An operational error is an expected error defined by the operation contract: a
native submission or completion failure, including a completed cancellation, is
reported by `uv::ops` as an error result. An input state such as a closed or busy
owner is also an error result when that operation documents it as a supported
state.

Loop-affinity violations, invalid lifetime or ownership use, double consumption,
and other broken API preconditions are programmer errors outside the result
channel. Diagnosable violations terminate rather than creating a recoverable
operational state. A result-returning API is not automatically `noexcept`:
allocation, user-provided constructors and moves, and documented C++ setup
failures can still throw. C++20 remains the baseline; `std::expected` is not a
dependency.

Copying-helper timing (construction versus startup) must be specified together
with setup-failure routing and the input lifetime before copying; see
[005](005-buffers-and-flow-control.md). Choosing an error surface must not change
that timing or the payload ownership policy.

## Exception Boundaries

Under `uv::ops`, native operational failures do not throw during setup,
submission, or completion: they become the operation's result. Exceptions that
remain are `std::bad_alloc`, exceptions from user-provided construction, move, or
callable code, other documented C++ preparation failures, and the explicit
`value()` access on an error result. The ergonomic facade unwraps the same
operational error at the await expression.

Keep the existing low-level callback termination policy documented until an explicit
replacement is accepted. No exception escapes a libuv C callback. Coroutine promises
and operation state deliver observed failures through awaits. A spawn handle
provides asynchronous join and result/exception observation. Destroying an active
handle requests cancellation while execution state survives through actual
completion and cleanup. `rethrow_if_failed()` and `take_result()` explicitly
observe a failure; `join()` does not. A failure left unobserved when the public
handle is lost is routed exactly once through a loop-level handler, whose default
policy terminates. Under the current loop-thread-only spawn-handle contract, the
handler runs on the loop thread; an escaping handler exception terminates. It may
replace or clear itself during delivery; that change
applies only to a subsequent failure. This applies even though public detach is deferred; see
[003](003-cancellation-and-task-scopes.md).

The optional posting component may also accept an error handler for posted
callables. Define behavior if an error handler throws. A loop-level
handler must not assume access to native `data` or silently change all callback
semantics.

## Alternatives and Open Questions

- Domain/member facade spellings and explicit operation initiation need migration
  examples before acceptance.
- Internal adaptation without duplicating public error-policy variants.
- Cleanup-error aggregation and allocation policy under the selected throwing /
  explicit-result surfaces.
- Whether exception-free builds are a separate supported target; this proposal alone
  does not promise support for disabling compiler exceptions.

## Implementation Progress and Validation

`uv::error_code`, `result<T>`, and `result<void>` are implemented in
[`core/error.hpp`](../../include/uvpp/core/error.hpp). Completion statuses and
existing domain error accessors use the new error vocabulary. The paired
throwing/explicit owner-close surface is implemented as `close()` and
`uv::ops::close(owner)` for TCP/pipe connections and listeners and UDP sockets.
DNS resolution now supplies the first paired request surface: `uv::resolve(...)`
throws native submission/completion failures and `uv::ops::resolve(...)` returns
`result<resolved_addresses>`, including completed `UV_ECANCELED`. It owns copied
values rather than exposing libuv's address list; resolver hints use the
value-type `resolve_options`, not native `addrinfo`. Filesystem now supplies the
first complete paired owner-I/O slice: `uv::fs::{open,read,write,close}` throws
native submission/completion failures, while `uv::ops::fs::{open,read,write,close}`
returns `result<file>`, `result<file_read_result>`, `result<size_t>`, and
`status`. Its close completion is terminal even on error, so result delivery
cannot trigger an unsafe descriptor retry. TCP/pipe `read_some`, `write`, and `accept`, and UDP `recv_from` and `send_to`
now have paired `uv::ops` facades, including read/write and UDP borrowed-view
forms. Reads return `result<Connection::read_some_result>` with successful EOF;
accept returns `result<tcp_connection>` or `result<pipe_connection>`; UDP receive
returns `result<udp_socket::recv_from_result>` with size, truncation, and copied
peer; write/send return `status`. The adapter constructs the original awaiter
in place and changes only result delivery. Native submission/completion,
closed/busy states, and completed cancellation enter the result channel without
altering callbacks, storage, slots, or cleanup. Failed/cancelled accepts still
wait for provisional child close. Both facades retain the same C++ preparation
exceptions (buffer length, allocation) and misuse diagnostics (loop/view).
No extra operation allocation is introduced. Connect and IPC handle-passing
adaptation remain unfinished. See the
[v3 error guide](../user/errors.md) for current availability.

Validate equivalent immediate/delayed native failures in both styles, allocation
failure during setup, EOF and partial results, move-only values, error access, and
unobserved-task routing. Test that native callbacks never propagate exceptions and
that result adaptation cannot miss failures during operation construction.

## Remaining low-level adaptation

Specialized completion types still differ in their direct convenience accessors:
stream/UDP reads and filesystem polling still lack direct `raw_status()` and
`error_code()` members. Migrated raw poll/fs-event results now provide both.
Filesystem success status is normalized to zero. Their common nested status is now
`uv::result<void>`, but uniform domain adaptation remains future work.

The old `tcp.try_bind()` example also has no implementation. Consider non-throwing
bind only as part of a deliberately scoped immediate-error surface using the naming
decision above. Preserve existing `loop.try_close()` and request cancellation until
a breaking change is adopted.

## Poll-source implementation progress

Poll readiness now pairs throwing `next(mask)` with
`uv::ops::next(source, mask) -> result<poll_events>`, covering both native start
and callback errors. Mask validation, busy, cancelled, and closed waits follow
the same policy. Construction remains throwing setup; cross-loop use remains
`std::logic_error`. See [the user contract](../user/poll.md).

## Filesystem-event implementation progress

`fs_event_source::next()` and `uv::ops::next(source) -> result<fs_event>`
share native callback, busy, closed, and cancellation failures. Both close
surfaces share native completion. Callback filename-copy exceptions are captured
and rethrown at either await expression. Constructor setup remains throwing,
and wrong-loop awaits raise `std::logic_error`.
See the [implemented contract](../user/fs-events.md). Resource-scope adoption
and broader platform/cost validation remain open.

## Network facade validation

[Network result tests](../../tests/test-co-network-ops.cpp) cover both stream
families, typed EOF, payload delivery, read/write exclusion, cancellation and
reuse, closed-owner results, wrong-loop rejection, accepted-owner transfer,
provisional-child close before cancelled/failed accept delivery, equivalent
fault-injected accept callback errors in both policies, borrowed views, and UDP truncation,
empty datagrams, and native send failure. Existing throwing lifecycle tests remain
in place. Connect and IPC pairs, broader fault injection, allocation-failure
routing, and cross-platform/performance gates remain open.

## Network factory validation

[Factory tests](../../tests/test-network-factories.cpp) cover IPv4/IPv6 success,
move-only result extraction with stable native addresses, equivalent native
bind/listen failures, and pending close storage through loop progress. Network
I/O tests now exercise factory-produced TCP/pipe listeners and UDP sockets. The
separate [allocation tests](../../tests/udp-allocation.cpp) inject C++ preparation
failures in both policies, including the legacy pipe-name copy, and verify no
native handle was initialized. Native init failure injection and broader
platform validation remain open.

The implemented `throw_if_error` helper now accepts `std::signed_integral` values
and preserves the argument's type and nonnegative payload. Only negative native
codes narrow to `int` for `uv::error`; explicit-result branches retain their
status checks and result construction. Core tests cover wide successful counts,
wide native errors, and compile-time rejection of unsigned/non-integral inputs.

Local Linux validation passes `make test-all GTEST_ARGS=--gtest_brief=1` with
GCC and Clang: each main suite passes 389 tests with two TTY skips, each isolated
allocation suite passes both cases, and examples build. Native Windows/macOS
validation of this helper change remains delegated to the platform CI jobs.

A dedicated `uv_errno_t` overload now forwards native enum constants through the
signed `int` specialization, preserving `throw_if_error(UV_EBADF)` convenience
without accepting arbitrary enums or unsigned values. Native integer counts
still select the type-preserving template; explicit-result operation branches
are unchanged. Core tests cover the enum overload's `int` return type, zero, and
native error delivery alongside integer and wide-count behavior.

The enum-overload follow-up passes local `make test-all GTEST_ARGS=--gtest_brief=1`
with GCC and Clang: 390 main cases pass with two TTY skips per compiler,
both isolated allocation cases pass, and examples build.

## Raw filesystem migration progress

`uv::raw::fs` callback and static submissions now return `uv::status` for immediate
native errors. Typed completion results preserve native payloads and normalized
error access. The high-level callback facade unwraps the status after cleaning
rejected native requests. C++ preparation failures remain exceptions. Explicit
cleanup, directory ownership rollback, and callback release/reuse are covered in
[test-fs.cpp](../../tests/test-fs.cpp); other raw families still need adaptation.


## Raw watcher migration progress

`uv::raw::{signal,poll,fs_event}` operations now report native start/stop failures
as `status`; common native getters use `result<T>` and setters use `status`.
Fs-event `path()` returns `result<std::string>` with C++ allocation still throwing.
Initialization remains separately documented throwing setup. Raw poll/fs-event
completion results normalize successful `raw_status()` to zero and expose direct
`error_code()`. High-level source waits and their `uv::ops` facades are unchanged.
See [the implemented contract](../user/raw-watchers.md).


## Raw timer and loop-phase migration progress

`uv::raw::{timer,idle,prepare,check}` now share migrated raw watcher storage,
explicit start/stop status and native queries, successful replacement commitment,
and capture release on stop/close. Timer one-shot completion extracts its callable
before user delivery; repeated callbacks retain mutable state. Timer `again`
requires an active subscription, and `set_repeat` reports duration/closing errors
as status. Raw duration validation and unsigned chrono getters are documented.
Idle/prepare/check restart native delivery to support active runtime/static
replacement. The shared loop, high-level coroutine sleep, and application-owned
raw `data` contracts remain unchanged. See [the implemented user contract](../user/raw-watchers.md#timers)
and [lifecycle tests](../../tests/test-raw-loop-watchers.cpp). Broader platform,
allocation-failure, and performance validation remain open; this is not an API
freeze or a high-level owner expansion.


## Raw async migration progress

The caller-owned wrapper is now `uv::raw::async`, with explicit `send` and callback
setter status/results. Its fixed native trampoline supports runtime/static
replacement, including reentrant replacement, without native reinitialization.
It shares raw callback/close/query machinery, adds one static function pointer,
and preserves application-owned native `data` and stable storage. Initialization
is active immediately and remains throwing setup; no start/stop API, queue, lock,
or producer endpoint is added. Only send is cross-thread safe. The caller must
prevent new sends and synchronize every in-flight send before close; send does not
read loop-thread closing state or promise a recoverable after-close error.
High-level `loop_posting` retains its independent synchronized native protocol.
See [the implemented user contract](../user/async.md) and
[raw async tests](../../tests/test-raw-async.cpp). Broader native platform,
allocation-failure, and performance gates remain open.

## Raw timer explicit initialization progress

Raw timer now offers `timer() noexcept`, `initialized()`, and
`init(loop&/loop_view) noexcept -> status`. Loop-taking constructors unwrap that
same result. Initialization is tracked solely through the native `loop` pointer;
it remains set through/after close, and repeat initialization returns
`UV_EALREADY`. Before init, operational methods return `UV_EBADF`, bool/duration
observations return false/zero, and ref/unref/close are inert. Views require
successful initialization. No added state, allocation, or native-data reservation
is needed. Lifecycle and callback-release coverage lives in
[`test-raw-timer-init.cpp`](../../tests/test-raw-timer-init.cpp).

This is also implemented for idle/prepare/check (see below). Other raw handle
families retain their current construction contracts; their failure/rollback paths need individual review
before extending this protocol. Libuv timer init has no native failure path on a
valid loop, so tests do not claim injected native-init failure coverage.

## Raw loop-phase explicit initialization progress

Idle, prepare, and check now share raw timer's default construction, status-returning
`init(loop&/loop_view) noexcept`, native-loop-pointer `initialized()`, and pre-init
observation/close/ref/query guards. Their throwing constructors unwrap `init()`;
start/static start and stop reject uninitialized handles with `UV_EBADF`. Repeat
init, including during/after close or with another loop, returns `UV_EALREADY`
without changing the existing native subscription or callable slot.

Common guards are factored into `detail::initializable_raw_watcher_handle`, also
used by timer, without a new data member, allocation, or native-data reservation.
Other watcher families retain their existing protocol. Phase native initialization
currently succeeds unconditionally on a valid loop in both Unix and Windows
backends; no synthetic init-failure contract is introduced. See the
[implemented user contract](../user/raw-watchers.md#idle-prepare-and-check) and
[portable typed tests](../../tests/test-raw-phase-init.cpp).
