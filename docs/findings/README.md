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

Statuses describe the current state, not an assumption about upstream acceptance:

- `suspected`: evidence suggests an issue, but the defect or its scope needs confirmation.
- `confirmed`: the behavior has been reproduced or otherwise established; upstream disposition may remain open.
- `reported`: an upstream issue or change has been submitted; link it and retain confirmation details.
- `resolved`: a fix or other resolution is available; identify its scope and versions.
- `closed`: investigation ended without an applicable fix, for example because the behavior is intended; explain why.

Update the status and references as evidence changes. One finding can distinguish
separate platform-specific confidence levels even when it has a single overall status.

## Findings

- [Large `uv_cond_timedwait` timeouts](uv-cond-timedwait-overflow.md) — confirmed on Linux; Windows behavior suspected.
- [Windows `uv_poll_init_socket` failure registration](uv-poll-init-windows-registration.md) — resolved upstream; fixed release not established.
