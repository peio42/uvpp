# Raw filesystem polling changes native active-restart status

- **Status:** confirmed; intentional wrapper policy is documented
- **Severity:** P2
- **Component:** `uv::raw::fs_poll::start` and `start_static`
- **Affected versions:** repository revision when built with libuv versions that provide `uv_fs_poll_t` (through 1.52.x)
- **Upstream:** no native defect identified.

## Observation

For an active filesystem-poll handle, uvpp returns `UV_EBUSY` when the caller
supplies a different path or interval. Native `uv_fs_poll_start()` returns
success immediately for any active handle and leaves its active context
unchanged. The wrapper therefore reports failure where native libuv reports
success and no-op behavior. For matching configuration uvpp additionally
replaces the user callback through its fixed native trampoline. That callback
replacement is a separate documented wrapper adaptation; the mismatch status is
the native-semantics divergence recorded here.

The differing policy is documented in the raw watcher guide and consistency
registry. That makes it deliberate, but it remains a semantic divergence in
`uv::raw`, whose design contract otherwise preserves native start/restart
behavior. The API is removed in libuv 1.53, but the issue applies when uvpp is
built with older header sets that still provide it.

## Evidence

- [`fs_poll::start`](../../include/uvpp/handles/fs_poll.hpp) checks the active
  path and interval and returns `UV_EBUSY` on mismatch.
- Native [`uv_fs_poll_start` in libuv 1.52.1](https://github.com/libuv/libuv/blob/v1.52.1/src/fs-poll.c)
  returns `0` when the handle is already active before examining new arguments.
- [The raw watcher guide](../user/raw-watchers.md#filesystem-polling) explicitly
  describes the mismatch restriction.
- [The consistency registry](../design/v3-consistency-registry.md) records the
  same callback replacement and mismatch policy.

## Reproduction

With libuv 1.52.x, initialize and start a poll on path A with interval X, then
call `start()` again while active with path B or interval Y. Native libuv returns
0 and retains the original active context. uvpp returns `UV_EBUSY`. No runtime
comparison was run during this audit.

## Recommended resolution

Reconcile the mismatch behavior with the raw-native contract. The closest native
behavior is to return success without changing the active configuration when
already active. Treat callback replacement as a separate explicit adapter
decision. If the stronger rejection policy is retained, record it as a reviewed
exception and make clear that it is a uvpp policy result rather than a native
submission failure.
