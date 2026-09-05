# Design Notes

These documents describe the implemented uvpp design and the rules for extending
it. They are primarily for contributors and maintainers. Future or incomplete
changes belong in [proposals](../proposals/index.md); acceptance of a proposal
does not by itself change the current API contracts.

- [Architecture](architecture.md): storage model, wrapper hierarchy, native interop, and repository layout.
- [API principles](api-principles.md): public API shape, naming, native access, user data, and value types.
- [API policy decisions](api-policy-decisions.md): concrete naming and result-shape policies for future API additions.
- [Callback strategy](callback-strategy.md): runtime callbacks, static callbacks, callback slots, trampolines, and exception boundaries.
- [Error handling strategy](error-handling-strategy.md): immediate failures, async completion results, EOF, and callback failure policy.
- [Ownership strategy](ownership-strategy.md): handle lifetime, request lifetime, buffers, user data, loop ownership, and deallocation rules.
- [Request guide](request-guide.md): step-by-step checklist for adding a new request type (classes, overloads, tests, documentation).
- [Thread safety](thread-safety.md): libuv threading model and cross-thread communication rules.
- [Threading primitives strategy](threading-primitives.md): implemented libuv thread and synchronization primitive wrappers.

## Implementation References

The design review distinguishes implemented behavior from contributor rules and
future proposals. These are the principal source/test pairs for maintaining that
alignment; the tests provide examples of coverage, not a proof of every contract.

| Area | Implementation | Existing tests |
| --- | --- | --- |
| Native identity and views | [native storage](../../include/uvpp/core/native.hpp), [handle views](../../include/uvpp/handles/handle_view.hpp) | [layout](../../tests/test-layout.cpp) |
| Loop and errors | [loop](../../include/uvpp/core/loop.hpp), [errors](../../include/uvpp/core/error.hpp) | [loop](../../tests/test-loop.cpp), [core](../../tests/test-core.cpp) |
| Callbacks and requests | [invocation boundary](../../include/uvpp/core/callback.hpp), [submission](../../include/uvpp/requests/request.hpp), [DNS](../../include/uvpp/requests/dns.hpp) | [core](../../tests/test-core.cpp), [network](../../tests/test-network.cpp) |
| Streams and UDP | [streams](../../include/uvpp/handles/stream.hpp), [UDP](../../include/uvpp/handles/udp.hpp) | [stream requests](../../tests/test-stream-requests.cpp), [UDP](../../tests/test-udp.cpp) |
| Filesystem ownership | [operations](../../include/uvpp/fs/operations.hpp), [directories](../../include/uvpp/fs/dir.hpp) | [filesystem](../../tests/test-fs.cpp), [watchers](../../tests/test-fs-watch.cpp) |
| Threading | [primitives](../../include/uvpp/threading/primitives.hpp), [work requests](../../include/uvpp/requests/work.hpp) | [threading](../../tests/test-threading.cpp), [work/random](../../tests/test-threadpool-random.cpp) |
| Feature availability | [capability macros](../../include/uvpp/core/version.hpp) | [validation gaps and proposed matrix](../proposals/010-validation-and-performance.md) |
