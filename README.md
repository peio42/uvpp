[![CI](https://github.com/peio42/uvpp/actions/workflows/ci-tests.yml/badge.svg)](https://github.com/peio42/uvpp/actions/workflows/ci-tests.yml)

# uvpp v3

uvpp is a header-only C++20 wrapper around [libuv](https://libuv.org/).
The repository and include root are `uvpp`; the public C++ namespace is `uv`.

This branch develops v3. The available coroutine tasks, scopes, and TCP, pipe,
and UDP owners are experimental. The complete raw/high-level split and paired
error surfaces are still being implemented; this is not a stable v3 release.
The development branch reports project version 3.0.0 as its target API version;
only a tagged release is a published stable version.
See the [v3 user guide](docs/user/index.md) for the available surface and limits.

The architecture shares one `uv::loop` between low-level operations, high-level
owners, and `uv::co` tasks. Owners retain stable native storage, asynchronous
cleanup stays explicit, and native libuv `data` belongs to application code.

## Quick start

Use focused headers for the APIs you need:

```cpp
#include <chrono>
#include <uvpp/co/sleep.hpp>

using namespace std::chrono_literals;

uv::co::task<int> answer() {
  co_await uv::co::sleep_for(10ms);
  co_return 42;
}

int main() {
  uv::loop loop;
  auto execution = uv::co::spawn(loop, answer());
  loop.run();
  loop.close();
  return execution.take_result() == 42 ? 0 : 1;
}
```

Compile with a C++20 compiler and link libuv and pthread:

```sh
g++ -std=c++20 -I/path/to/uvpp/include app.cpp -luv -pthread -o app
```

[Getting started](docs/user/getting-started.md) covers CMake integration and the
execution/cleanup order. The umbrella header is `<uvpp/uv.hpp>`; focused coroutine
headers make their dependencies explicit.

## Build and validation

Install a C++20 compiler, libuv, and Google Test for repository tests.

```sh
make test          # default compiler suite and examples
make test-all      # GCC and Clang suites
make examples      # example binaries in build/<compiler>/examples/
make package VERSION=<package-version>
```

For focused work, run `make test-filter TEST_FILTER='Suite.Name'`. Add
`GTEST_ARGS=--gtest_brief=1` to a test command to show only test failures.

Packaging produces `dist/uvpp-<package-version>.tar.gz` and `dist/checksums.txt`.
The generated archive contains its `VERSION` file; packaging does not modify the
checkout.

When building a checkout with CMake, `UVPP_VERSION` can supply an explicit
release identity (for example, `-DUVPP_VERSION=3.0.0-rc.1`). Otherwise CMake
derives a build identity from Git. `PROJECT_VERSION` remains the numeric
`major.minor.patch` compatibility version used by CMake packages.
The examples directory still includes low-level programs awaiting API migration;
the v3 introduction is the program above and the user guides below.

## Documentation

- [User guides](docs/user/index.md): available v3 APIs, examples, and limitations.
- [Design](docs/design/index.md): implementation contracts and contributor rules.
- [Proposals](docs/proposals/index.md): remaining work, open decisions, and progress.

## License

uvpp is distributed under the [MIT License](LICENSE).
