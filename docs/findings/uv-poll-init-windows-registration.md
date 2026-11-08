# Windows poll initialization can fail after registration

- **Status:** resolved upstream; release containing the fix not established
- **Component:** libuv `uv_poll_init_socket` on Windows (also reached by the integer descriptor overload)
- **Affected versions:** official source tags 1.44.2 through 1.53.0 as listed in the [audit](../design/raw-poll-init-rollback.md); native test jobs are configured for 1.45.0 and 1.53.0, but their execution was not established by that audit
- **Upstream:** [libuv PR #5379](https://github.com/libuv/libuv/pull/5379) was merged into `v1.x` on 2026-10-31 as [commit `dd9f081`](https://github.com/libuv/libuv/commit/dd9f081cc55a4d83e2a976848d074f28ee5f34eb). It moves registration after the protocol query. A release containing the fix has not been established.

## Observation and expected behavior

If the Windows `SO_PROTOCOL_INFOW` query fails after native handle registration,
initialization returns an error while the inactive poll handle remains registered
with the loop. The expected failure boundary for uvpp's raw initialization is
that a failed initialization leaves no registered handle and allows immediate
destruction or retry. The upstream PR identifies the same failure as a stale registration that can
cause invalid memory access if the failed handle is released. A naturally
occurring provider failure has not been demonstrated in this investigation.

## Evidence and analysis

Source inspection shows registration before the protocol query; backend selection
and request initialization occur afterward. The Windows tests inject a Winsock
query failure into the actual linked libuv and observe the registered, inactive
handle using `uv_walk` inside that query. This is fault injection, not a naturally
occurring failure or an independent standalone native reproducer. The
[design audit](../design/raw-poll-init-rollback.md) records the version-by-version
source links, test method, and limits, along with uvpp's existing rollback decision.

To reproduce the tested boundary on Windows, build and run the native tests in
[`tests/windows-poll-native`](../../tests/windows-poll-native/CMakeLists.txt),
including the `test-raw-poll-windows.cpp` cases. The upstream PR reports that its original fault-injection regression test failed
before the fix and passed afterward with static and shared builds. That test was
subsequently removed from the PR. An independent native reproducer remains
follow-up work.
