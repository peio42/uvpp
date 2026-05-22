# AGENTS.md

## Project

uvpp v2 is a header-only C++20 wrapper around libuv.

The repository and include root use the `uvpp` name. The public C++ API lives in namespace `uv`:

```cpp
#include <uvpp/uv.hpp>

uv::loop loop;
uv::timer timer(loop);
```

## Build And Test

- Build and run the default test suite with `make test`.
- Run both compiler suites with `make test-all`.
- Build examples with `make examples`.
- Build a local package with `make package VERSION=2.1.0`.
- Use C++20 and link applications with libuv and pthread.

## Coding Rules

- Keep wrappers address-stable after native initialization.
- Do not make handles copyable or movable.
- Do not store wrapper internals in libuv `data`; `data` belongs to application code.
- Keep raw libuv access explicit through named helpers such as `native()`, `native_handle()`, and `native_stream()`.
- Do not add implicit conversions to raw libuv pointers or borrowed view types.
- Produce borrowed views through explicit `.view()` functions, such as `timer.view()` or `buffer.view()`.
- Reserve `try_*` for non-throwing variants that return `std::error_code` or an equivalent explicit status channel.
- Name synchronous immediate libuv operations `*_now()` even when the libuv function uses `try`, such as `send_now()` or `write_now()`.
- Use typed result objects when an immediate libuv return value mixes payload and status, such as byte counts plus `UV_EAGAIN`.
- Use C++20 concepts for public templates that depend on structural API contracts, such as stream-like handles or callback invocability.
- Prefer references in callbacks when null is not valid.
- Report asynchronous completion through `uv::result` or typed result objects.
- Throw `uv::error` for immediate submission failures in the primary low-level API.
- Use `std::chrono` for public duration values; keep counters as integer counts.
- Keep asynchronous lifetime visible through handle/request ownership.
- Keep low-level wrappers allocation-free unless the API explicitly owns operation state.
- Do not let exceptions escape from libuv C callbacks.

## Documentation Layout

- User documentation lives in `docs/`.
- Design and coding strategy lives in `docs/design/`.
- Keep `README.md` focused on project overview, quick start, build, and links.
- Put user-facing explanations in `docs/*.md`, not in design strategy files.

## Design References

Read these before changing public API shape or callback/lifetime behavior:

- `docs/design/architecture.md`
- `docs/design/api-principles.md`
- `docs/design/api-policy-decisions.md`
- `docs/design/callback-strategy.md`
- `docs/design/error-handling-strategy.md`
- `docs/design/ownership-strategy.md`
- `docs/design/thread-safety.md`
