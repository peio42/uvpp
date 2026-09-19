# Adding a request or operation

New work follows the [v3 architecture](architecture.md), not the namespace or
throwing submission policy of an older wrapper. First identify the layer and
read the relevant ownership, operation, buffer, and error proposals.

## Define the public contract

For a raw operation, make caller-controlled request and buffer lifetimes explicit
and return operational status/results. A high-level operation owns its control
state; borrowing still defaults for write/send bytes. Throwing and `uv::ops`
frontends must share submission, completion, rollback, and cleanup behavior.
Do not add a public operation until its layer and setup-failure policy are clear.

Use `uv::status` or `uv::result<T>` for common outcomes. Keep richer domain types
where EOF, partial progress, would-block, or borrowed data needs representation.
Do not copy the differing [existing result-family](error-handling-strategy.md#result-families)
accessors into a new common result contract.

## Implement lifecycle machinery

Keep native requests address-stable and reconstruct state without native `data`.
Document callback/frame ownership and every input retained by libuv. Roll back
claims and owned preparation state on submission failure; separately cover failure
before submission, including callable and string allocation.

At terminal completion, quiesce any native source, release claimed slots, and
extract callbacks before user delivery. Transfer/free native results even with no
callback. Do not access storage after delivery if user code can destroy it.
Cancellation requests do not imply completion. No exception may cross C callbacks.

Existing `detail::submit_request` helpers throw on immediate failure. Their
rollback behavior is useful evidence, but they are not a ready-made implementation
of the v3 raw result policy. Persistent subscriptions need their own protocol.

## Integration and validation

Add focused headers and umbrella includes as appropriate. Apply named capability
macros consistently to declarations and tests; see
[version gating](api-policy-decisions.md#version-gated-libuv-features).

Validate native layout/reconstruction, success, setup/submission failure rollback,
exactly-once completion, resubmission or destruction from completion, cancellation,
borrowed lifetimes, and native close retention. Exercise both error surfaces when
implemented and static/runtime delivery where supported. Use existing tests as
evidence, not as a substitute for the new operation's lifetime contract.

Update the relevant v3 user guide, implemented design, and proposal progress in
the same change. Keep remaining work explicit instead of documenting sketches as
available API.
