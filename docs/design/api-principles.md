# API principles

These are contributor rules for v3, under
[proposal 000](../proposals/000-v3-architecture.md). Existing code that differs
requires migration; it does not override the target contract.

- Keep C++20, header-only integration, focused headers, and one shared `uv::loop`.
- Use `uv::raw` for explicit low-level protocols, high-level `uv` owners,
  `uv::co` composition, and `uv::ops` result-oriented operations on those owners.
  The complete namespace split is not yet implemented.
- Keep native addresses stable. Raw handles are not movable; an owner can move
  stable storage without relocating a native handle or invalidating callbacks.
- Make borrowing explicit through `.view()` and native access explicit through
  named helpers. Do not add implicit pointer or view conversions.
- Reserve native `data` for application code. Typed access is a cast convenience,
  not dynamic type checking or ownership.
- Borrow write/send bytes by default. Copying and ownership transfer need explicit
  semantic names or types. Operation ownership does not imply payload ownership.
- Expose asynchronous completion and cleanup. Cancellation is not completion;
  destructors cannot drive a nested loop or release live native storage.
- Use structural C++20 concepts for public templates, references when null is invalid,
  chrono for durations, and integer counts for metrics.
- Preserve EOF, partial progress, would-block, and domain-specific results.
  Raw operational errors are explicit; high-level awaits throw at the await;
  `uv::ops` awaits return operational errors. Setup/allocation policies are separate.
- Release terminal operation slots before user delivery. Persistent subscriptions
  retain exclusivity until their native source has been quiesced.
- Keep loop-thread affinity unless an entry point is explicitly cross-thread safe.
  No exception may escape a libuv C callback.
- Document allocations and ownership costs. Do not infer an allocation-free
  operation from static callback dispatch alone.

See [naming and API shape](naming-and-api-shape.md),
[API policy decisions](api-policy-decisions.md), and
[implementation architecture](architecture.md) for availability boundaries.
