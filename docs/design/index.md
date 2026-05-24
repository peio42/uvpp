# Design Notes

These documents describe uvpp's API and implementation strategy. They are primarily for contributors and maintainers.

- [Architecture](architecture.md): storage model, wrapper hierarchy, native interop, and repository layout.
- [API principles](api-principles.md): public API shape, naming, native access, user data, and value types.
- [API policy decisions](api-policy-decisions.md): concrete naming and result-shape policies for future API additions.
- [Callback strategy](callback-strategy.md): runtime callbacks, static callbacks, callback slots, trampolines, and exception boundaries.
- [Coroutine strategy draft](coroutine-strategy.md): target coroutine API direction, task/awaitable layers, stream abstractions, and open design decisions.
- [Error handling strategy](error-handling-strategy.md): immediate failures, async completion results, EOF, and callback failure policy.
- [Ownership strategy](ownership-strategy.md): handle lifetime, request lifetime, buffers, user data, loop ownership, and deallocation rules.
- [Request guide](request-guide.md): step-by-step checklist for adding a new request type (classes, overloads, tests, documentation).
- [Thread safety](thread-safety.md): libuv threading model and cross-thread communication rules.
- [Threading primitives strategy](threading-primitives.md): target API shape for libuv thread and synchronization primitive wrappers.
- [v3 design notes](v3-notes.md): possible breaking-release cleanups and naming changes.
