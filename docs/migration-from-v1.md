# Migration from v1

## Direction

uvpp v2 is a breaking redesign. v1 is a reference for coverage and behavior, not a compatibility constraint.

uvpp v2 targets C++20. Porting code to v2 also means accepting the C++20 baseline.

The migration goal is not to make v1 code compile unchanged. The goal is to provide a safer, clearer C++ API with a small runtime footprint.

## Intentional Breaking Changes

v2 should intentionally change:

- no wrapper inherits from libuv C structs;
- raw fields are not normal public API;
- handles are non-copyable and non-movable;
- callbacks no longer require users to write casts;
- async status values are delivered as result objects;
- buffer ownership is represented by type;
- `std::chrono`, `std::span`, and `std::string_view` are used where appropriate.

## What to Reuse from v1

Use v1 for:

- the list of libuv objects to cover;
- existing tests as behavioral scenarios;
- examples as use cases to rewrite;
- known bugs as anti-regression cases;
- naming hints where the names are already clear.

Do not reuse v1's callback machinery or inheritance strategy.

## Suggested Migration Map

Examples:

```cpp
// v1
uv::Loop* loop = uv::Loop::getDefault();
uv::Timer timer(loop);
timer.start(callback, 1000);
```

```cpp
// v2
uvpp::loop_view loop = uvpp::default_loop();
uvpp::timer timer(loop);
timer.start(1s, callback);
```

```cpp
// v1
uv::Tcp server(loop);
uv::IPv4 addr("0.0.0.0", 2345);
server.bind(&addr);
```

```cpp
// v2
uvpp::tcp server(loop);
server.bind(uvpp::ipv4{"0.0.0.0", 2345});
```

## Compatibility Layer

A compatibility layer is optional and should not block v2.

If implemented, it should live separately:

```text
include/uvpp/compat/v1/
```

It can adapt common v1 names to v2 types, but it should not force v2 internals to mimic v1.

## Porting Order

Port examples and tests after the v2 vertical slice exists:

1. timer tests
2. idle/check/prepare tests
3. TCP echo example
4. TCP read stop/start test
5. async test
6. signal tests
7. filesystem examples
8. UDP example

Each port should improve semantics instead of preserving v1 quirks.
