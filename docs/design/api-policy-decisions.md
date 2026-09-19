# API policy decisions

These v3 contributor rules complement [naming and API shape](naming-and-api-shape.md).
Current declarations that differ remain migration work, not exceptions for new APIs.

## Naming and error policy

Reserve `try_*` for actual attempt semantics, such as `try_lock`. Use `*_now` for
synchronous immediate I/O, including libuv functions named `try`. Typed immediate
results preserve byte counts, would-block, and errors. Do not add `_status`, `_ec`,
or `async_` merely to select an error channel.

Use high-level throwing awaits and `uv::ops` explicit-result awaits on the same
owners. Both must cover submission and completion failure without changing payload
ownership. Only the owner-close pair is currently implemented across network
owners. Existing low-level `loop.try_close()` and request `try_cancel()` spellings
have not yet been consolidated; do not replicate their error-policy convention.

## Ownership and public shape

Borrow payloads by default; name copying and transfer explicitly. Produce borrowed
views with `.view()`, and use named native accessors without implicit conversions.
Use role names such as `tcp_connection` and `tcp_listener`, not a separate coroutine
I/O hierarchy. Keep `uv::loop` canonical; overloads taking borrowed loop views
must not imply that every coroutine entry point accepts one today.

Use concepts for structural public contracts, references where null is invalid,
namespace-level typed constant domains, chrono durations, and integer counters.
Blocking helpers must make blocking clear. An immediate operation must not allocate
hidden request or batch metadata; an explicitly owning batch value may do so.
Ranges must document borrowing and single-pass behavior. Fluent option values are
useful for substantial named configuration, not a requirement for every argument list.

## Version-Gated Libuv Features

uvpp is header-only and compiles against the libuv headers available to the
consumer. Public wrappers for libuv APIs or constants introduced after the
project's practical baseline must be gated at compile time.

Use named capability macros from `uvpp/core/version.hpp` instead of repeating
raw `UV_VERSION_HEX` comparisons in feature headers or tests:

```cpp
#if UVPP_HAS_UDP_TRY_SEND2
auto result = udp.send_many_now(batch);
#endif
```

The macro name should describe the libuv capability, not the wrapper spelling.
For example, prefer `UVPP_HAS_UDP_TRY_SEND2` over
`UVPP_HAS_UDP_SEND_MANY_NOW`.

Apply the same capability macro to:

- public types that depend on a newer libuv constant;
- public member functions that call a newer libuv function;
- tests that reference the gated API;
- documentation examples when the feature may not exist for all supported
  libuv packages.

Do not provide a stub member that compiles but always returns "unsupported" when
the underlying declaration is absent. If the libuv header cannot declare the
native function or constant, the uvpp wrapper should not declare that specific
API either. Users can test the `UVPP_HAS_*` macro when writing portable code.

Prefer documented libuv introduction versions. When the docs only describe a
behavior change and not the original symbol introduction, use the earliest
version that is known to expose the symbol and keep the decision centralized in
`version.hpp`.


## Results and validation

`uv::status` aliases `uv::result<void>`. Generic results expose `has_value()`,
boolean conversion, `value()`, and `error()`. Preserve richer domain outcomes
instead of flattening EOF, partial reads, or would-block into a generic error.
See [result families](error-handling-strategy.md#result-families) for current
low-level differences that still need adaptation.

Test native address stability, immediate failure rollback, terminal callback-slot
release, resubmission, cancellation, close completion, and borrowed lifetimes.
Update user documentation only for APIs actually available in the v3 slice;
record missing surfaces and open decisions in the relevant proposal.
