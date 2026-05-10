[![CI](https://github.com/peio42/uvpp/actions/workflows/ci-tests.yml/badge.svg)](https://github.com/peio42/uvpp/actions/workflows/ci-tests.yml)

# uvpp v2

uvpp v2 is a modern, header-only C++20 wrapper around [libuv](https://libuv.org/).

The goal of v2 is to keep libuv's model visible while providing a cleaner C++ API:

- no inheritance from libuv C structs;
- no user-facing casts in callbacks;
- explicit ownership and lifetime rules;
- modern C++ vocabulary types such as `std::chrono`, `std::span`, and `std::string_view`;
- explicit error handling through `uvpp::error` and `uvpp::result`.

## Status

uvpp v2 is under active construction on the `v2` branch.

It is already usable for the current vertical slice, but it is not feature-complete yet. The API should still be considered evolving until v2 is finalized.

## Requirements

- C++20 compiler
- libuv

For repository builds and tests, the current setup also uses Google Test.

## Integration

uvpp v2 is header-only. Add the `include/` directory to your include path and link against libuv.

```cpp
#include <uvpp/uv.hpp>
```

Typical compile command:

```sh
clang++ -std=c++20 -I/path/to/uvpp/include app.cpp -luv -pthread
```

## Current surface

The current v2 slice includes:

- loop types: `uvpp::loop`, `uvpp::loop_view`, `uvpp::default_loop()`
- error types: `uvpp::error`, `uvpp::result`
- networking helpers: `uvpp::ipv4`, `uvpp::ipv6`, `uvpp::buffer_view`
- requests: `uvpp::connect_request`, `uvpp::write_request`
- handles: `uvpp::async`, `uvpp::check`, `uvpp::idle`, `uvpp::pipe`, `uvpp::prepare`, `uvpp::tcp`, `uvpp::timer`, `uvpp::tty`

## Core usage patterns

### Create or access a loop

```cpp
uvpp::loop loop;
auto default_loop = uvpp::default_loop();
```

### Runtime callbacks

Runtime callbacks accept lambdas with captures and receive typed wrapper references.

```cpp
using namespace std::chrono_literals;

uvpp::loop loop;
uvpp::timer timer(loop);

int ticks = 0;

timer.start(100ms, 100ms, [&](uvpp::timer& self) {
  if (++ticks == 3) {
    self.close();
  }
});

loop.run();
loop.close();
```

### Static callbacks

For zero-allocation callback paths, use the `*_static` APIs.

```cpp
using namespace std::chrono_literals;

static void on_tick(uvpp::timer& self) {
  self.close();
}

uvpp::loop loop;
uvpp::timer timer(loop);

timer.start_static<on_tick>(250ms);
loop.run();
loop.close();
```

### Explicit close and lifetime

`uv_close()` is asynchronous in libuv, so uvpp v2 keeps that visible. Handles do not auto-close in destructors.

```cpp
auto* client = new uvpp::tcp(loop);

client->close([client](uvpp::tcp&) {
  delete client;
});
```

### Error handling

- immediate libuv submission failures throw `uvpp::error`;
- asynchronous completion status is delivered as `uvpp::result`;
- EOF is represented explicitly in `uvpp::read_result`.

```cpp
try {
  uvpp::tcp server(loop);
  server.bind(uvpp::ipv4{"0.0.0.0", 2345});
} catch (const uvpp::error& e) {
  std::cerr << e.what() << '\n';
}
```

## Example

The repository ships a TCP echo server example in `examples/tcp-echo-server.cpp`.

Minimal server setup:

```cpp
uvpp::loop loop;
uvpp::tcp server(loop);

server.bind(uvpp::ipv4{"0.0.0.0", 2345});
server.listen([&](uvpp::tcp& srv, uvpp::result status) {
  if (!status) {
    return;
  }

  auto* client = new uvpp::tcp(loop);
  srv.accept(*client);
  // start reading and writing...
});

loop.run();
```

## Building and testing this repository

Ubuntu packages:

```sh
sudo apt-get update
sudo apt-get install -y clang libgtest-dev libuv1-dev
```

Then build the example and run the test suite:

```sh
make test
```

## Documentation

- [Architecture](docs/architecture.md)
- [API principles](docs/api-principles.md)
- [Callbacks](docs/callbacks.md)
- [Error handling](docs/error-handling.md)
- [Ownership and lifetime](docs/ownership.md)
- [Thread safety](docs/thread-safety.md)
- [Migration from v1](docs/migration-from-v1.md)
