[![CI](https://github.com/peio42/uvpp/actions/workflows/ci-tests.yml/badge.svg)](https://github.com/peio42/uvpp/actions/workflows/ci-tests.yml)
[![Version](https://img.shields.io/github/v/tag/peio42/uvpp?label=version&sort=semver)](https://github.com/peio42/uvpp/tags)

# uvpp v2

uvpp is a header-only C++20 wrapper around [libuv](https://libuv.org/).

The project keeps the `uvpp` name for the repository and include path, while the public C++ API lives in namespace `uv`:

```cpp
#include <uvpp/uv.hpp>

uv::loop loop;
uv::timer timer(loop);
```

uvpp is a C++ API over libuv with explicit lifetime rules, typed callbacks, native interop, and a small compiled footprint.

## Why uvpp?

- Header-only integration: add `include/` to your include path and link with libuv.
- C++20 vocabulary: `std::chrono`, `std::span`, `std::string_view`, typed value wrappers, and move-aware result types.
- No user-facing casts in normal callbacks: callbacks receive `uv::tcp&`, `uv::write_request&`, `uv::fs::read_result`, etc.
- Explicit native interop: use `native()`, `native_handle()`, or `native_stream()` when you need raw libuv access.
- Application-owned `data`: libuv `data` fields remain available through `user_data<T>()`.
- Two callback styles: ergonomic runtime callables and zero-overhead `template<auto Callback>` static callbacks.
- Explicit async lifetime: handles do not pretend that `uv_close()` is synchronous.
- Filesystem layering: `uv::fs` is safe-by-default, while `uv::fs::raw` exposes the exact libuv request protocol.

## Status

uvpp 2.0.0 is the first stable release. It covers the core loop, all main handle families, stream requests, UDP, filesystem operations (`uv::fs` and `uv::fs::raw`), watchers, and process spawning. Post-2.0.0 additions are tracked in [Future features](docs/future.md).

## Requirements

- C++20 compiler
- libuv
- Google Test, only for building this repository's test suite

Typical compile command:

```sh
g++ -std=c++20 -I/path/to/uvpp/include app.cpp -luv -pthread
```

CMake users can consume the `uvpp::uvpp` target directly — see [Getting started](docs/getting-started.md).

## Quick Start

```cpp
#include <chrono>
#include <iostream>

#include <uvpp/uv.hpp>

using namespace std::chrono_literals;

int main() {
  uv::loop loop;
  uv::timer timer(loop);

  int ticks = 0;

  timer.start(100ms, 100ms, [&](uv::timer& self) {
    std::cout << "tick\n";

    if (++ticks == 3) {
      self.close();
    }
  });

  loop.run();
  loop.close();
}
```

## Callback Models

Runtime callbacks are the ergonomic default. They can capture state and are stored in the wrapper or request object.

```cpp
uv::timer timer(loop);
int ticks = 0;

timer.start(250ms, [&](uv::timer& self) {
  if (++ticks == 1) {
    self.close();
  }
});
```

Static callbacks avoid storing a callable and are useful for zero-allocation paths.

```cpp
static void on_tick(uv::timer& timer) {
  timer.close();
}

uv::timer timer(loop);
timer.start_static<on_tick>(250ms);
```

Immediate submission failures throw `uv::error`. Asynchronous completion is reported through `uv::result` or a typed result object.

```cpp
try {
  uv::tcp server(loop);
  server.bind(uv::ipv4{"0.0.0.0", 2345});
} catch (const uv::error& error) {
  std::cerr << error.what() << '\n';
}
```

## Handles and Requests

The implemented handle surface currently includes:

- Streams: `uv::tcp`, `uv::pipe`, `uv::tty`
- Datagram sockets: `uv::udp`
- Simple loop handles: `uv::timer`, `uv::async`, `uv::prepare`, `uv::check`, `uv::idle`
- System handles: `uv::signal`, `uv::poll`, `uv::process`
- Filesystem watchers: `uv::fs_event`, `uv::fs_poll`

Stream and UDP operations use explicit request objects when libuv requires an async request lifetime:

```cpp
uv::connect_request connect;
uv::write_request write;
uv::shutdown_request shutdown;
uv::udp_send_request send;
```

This keeps ownership visible and avoids hidden per-operation allocation in the low-level API.

## Streams

Streams expose libuv's accept/read/write model with typed wrappers.

```cpp
uv::tcp server(loop);
server.bind(uv::ipv4{"127.0.0.1", 2345});

server.listen([&](uv::tcp& srv, uv::result status) {
  if (!status) {
    return;
  }

  auto* client = new uv::tcp(loop);
  srv.accept(*client);

  client->close([client](uv::tcp&) {
    delete client;
  });
});
```

`uv_close()` is asynchronous. Handles therefore do not auto-close in their destructor; close explicitly and keep the wrapper alive until the close callback has run.

## Buffers

uvpp v2 separates owning storage from borrowed views:

- `uv::buffer_view`: non-owning `uv_buf_t`-compatible view.
- `uv::owned_buffer`: owning byte storage that can create a `buffer_view`.
- `std::span<std::byte>` / `std::span<const std::byte>`: generic byte ranges.

```cpp
uv::owned_buffer storage{4096};
uv::buffer_view view = storage.view();
```

Low-level async write and UDP send operations do not copy submitted buffers. The referenced memory must outlive the completion callback.

## Filesystem

Filesystem APIs are split into two layers.

`uv::fs` is the recommended C++ layer. It owns the internal `uv_fs_t`, cleans it automatically, and returns scalar or owned results that may outlive the callback.

```cpp
#include <fcntl.h>
#include <span>

std::array payload{'o', 'k'};

uv::fs::open(loop, "out.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644,
  [&](uv::fs::open_result open) {
    if (!open) {
      return;
    }

    uv::file_descriptor file = open.file();

    uv::fs::write(loop, file, std::as_bytes(std::span{payload}), 0,
      [&, file](uv::fs::byte_count_result written) {
        uv::fs::close(loop, file, [](uv::fs::status_result) {});
      });
  });
```

`uv::fs::raw` is the libuv-shaped layer. It exposes `uv::fs::raw::request`, manual `cleanup()`, request reuse, static callbacks, and request-scoped result views.

```cpp
uv::fs::raw::request request;

uv::fs::raw::stat(loop, request, "out.txt",
  [](uv::fs::raw::request& request, uv::fs::raw::stat_result result) {
    auto cleanup = request.scoped_cleanup();

    if (result) {
      auto size = result.native().st_size;
      (void)size;
    }
  });
```

## Native Interop and User Data

Raw libuv access is explicit:

```cpp
uv_tcp_t* raw_tcp = tcp.native();
uv_stream_t* raw_stream = tcp.native_stream();
uv_handle_t* raw_handle = tcp.native_handle();
```

libuv `data` fields are reserved for the application, not for wrapper internals.

```cpp
struct session {};

session state;
tcp.user_data(state);

auto* current = tcp.user_data<session>();
tcp.clear_user_data();
```

`user_data<T>()` is a typed cast convenience over libuv's `void*`. It is intentionally zero-overhead and does not make the field type-safe.

## Building This Repository

Ubuntu packages:

```sh
sudo apt-get update
sudo apt-get install -y g++ libgtest-dev libuv1-dev
```

Build examples and run tests:

```sh
make test
```

Run the same suite with both supported compilers:

```sh
make test-all
```

Or target one compiler explicitly:

```sh
make test-gcc
make test-clang
```

Build only:

```sh
make build
make build-all
```

Remove generated objects and binaries:

```sh
make clean
```

Create a local header-only package:

```sh
make package VERSION=2.0.0
```

This produces:

- `dist/uvpp-2.0.0.tar.gz`
- `dist/checksums.txt`

The CI workflow runs the GCC and Clang suites on pull requests and on pushes to `main`. Branch pushes are intentionally not tested separately to avoid duplicating the same commit validation when a pull request is open.

The manual release workflow takes a version, verifies that the GCC and Clang CI checks already passed for the commit, creates the same package, pushes an annotated `v<version>` tag, and publishes a GitHub Release for that tag. If the checks are missing, dispatch the release with `run_tests=true` to run the full validation suite inside the release workflow.

Release assets:

- `uvpp-<version>.tar.gz`
- `checksums.txt`

Versions containing a prerelease suffix such as `2.0.0-rc.1` are published as GitHub prereleases.

## Documentation

- [Documentation index](docs/index.md)
- [Tutorial](docs/tutorial/index.md)
- [Getting started](docs/getting-started.md)
- [Callbacks](docs/callbacks.md)
- [Errors](docs/errors.md)
- [Ownership and lifetime](docs/ownership-and-lifetime.md)
- [Buffers](docs/buffers.md)
- [Streams](docs/streams.md)
- [UDP](docs/udp.md)
- [Filesystem](docs/filesystem.md)
- [Process](docs/process.md)
- [Future features](docs/future.md)

Contributor design notes live under [docs/design](docs/design/).

The repository also ships runnable examples under [`examples/`](examples/), including TCP,
UDP, timer/watchers, and process stdout capture.

## License

uvpp v2 is distributed under the MIT License. See [LICENSE](LICENSE).
