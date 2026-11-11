# Technical findings

This directory records significant defects, unexpected behavior, portability
issues, and limitations discovered during reviews and audits. Findings preserve
the investigation history after resolution; design documents describe uvpp's
contracts and decisions and link to relevant findings.

Each finding should identify its status, affected component and versions, the
expected and observed behavior, supporting evidence, reproduction steps when
available, and relevant upstream issue or fix references. Separate observations
from hypotheses and source inspection from runtime validation. Use `unknown`
where evidence is not yet available.

Severity uses this audit scale: `P1` blocks common builds or risks broad loss of
correctness; `P2` affects a specific operation or its resource lifecycle; `P3`
is a lower-impact API consistency or contract clarity issue.

Statuses describe the current state, not an assumption about upstream acceptance:

- `suspected`: evidence suggests an issue, but the defect or its scope needs confirmation.
- `confirmed`: the behavior has been reproduced or otherwise established; upstream disposition may remain open.
- `reported`: an upstream issue or change has been submitted; link it and retain confirmation details.
- `resolved`: a fix or other resolution is available; identify its scope and versions.
- `closed`: investigation ended without an applicable fix, for example because the behavior is intended; explain why.

Update the status and references as evidence changes. One finding can distinguish
separate platform-specific confidence levels even when it has a single overall status.

## Findings

- [Raw libuv workaround candidate inventory](raw-libuv-workaround-inventory.md) — targeted inventory of native deviations and rejected candidates.
- [One-shot signal rearm during callback](upstream-signal-oneshot-rearm.md) — reproduced on Linux; upstream contract and disposition unclear.
- [Raw v3 audit summary](raw-v3-audit-summary.md) — scope, priorities, and unresolved follow-up.
- [Raw filesystem requests do not reject pending reuse](raw-fs-request-pending-reuse.md) — confirmed missing guard across runtime and static submissions.
- [Cancelled raw `closedir` loses its typed owner](raw-closedir-cancellation-ownership.md) — confirmed ownership gap on native cancellation.
- [Raw filesystem polling reports `UV_EBUSY` for native no-op restarts](raw-fs-poll-active-restart.md) — confirmed intentional status divergence.
- [Raw watcher close behavior differs by family](raw-watcher-close-consistency.md) — confirmed duplicate-close guard inconsistency.
- [libuv filesystem polling can miss changes before its first stat](upstream-fs-poll-initial-stat.md) — source-confirmed baseline behavior; upstream discussion continues.
- [Large `uv_cond_timedwait` timeouts](uv-cond-timedwait-overflow.md) — confirmed on Linux; Windows behavior suspected.
- [Windows `uv_poll_init_socket` failure registration](uv-poll-init-windows-registration.md) — resolved upstream; fixed release not established.
