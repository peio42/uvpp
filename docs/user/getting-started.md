# Getting started

Use the v3 development checkout with a C++20 compiler, libuv, and pthread.
The APIs in this guide are implemented but experimental.

## A complete program

Save this as `app.cpp`:

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

```sh
g++ -std=c++20 -I/path/to/uvpp/include app.cpp -luv -pthread -o app
./app
```

The task is cold until `spawn` binds it to the loop and starts it. Keep the spawn
handle alive, drive the loop through timer completion and native cleanup, then
close the loop. `take_result()` moves the completed value or rethrows the task's
exception. For `task<void>`, use `rethrow_if_failed()` after completion.

Use focused headers such as `<uvpp/co/task.hpp>`, `<uvpp/co/sleep.hpp>`, and
`<uvpp/net/tcp_connection.hpp>`. `<uvpp/uv.hpp>` is the umbrella header; it also
includes low-level APIs whose v3 migration remains unfinished.

## CMake integration

With this checkout alongside the application:

```cmake
cmake_minimum_required(VERSION 3.14)
project(myapp LANGUAGES CXX)
add_subdirectory(uvpp)
add_executable(myapp app.cpp)
target_link_libraries(myapp PRIVATE uvpp::uvpp)
```

The interface target propagates C++20, include paths, libuv, and thread dependencies.
For an installed checkout, replace `add_subdirectory(uvpp)` with
`find_package(uvpp REQUIRED)`. Pin the development revision you validated when
fetching the dependency; these guides do not imply a published stable v3 package.

## Repository checks

Google Test is additionally required for the repository tests.

```sh
make test
make test-all
make examples
```

Continue with [loop execution](loop.md), [coroutines](coroutines.md), and
[ownership and cleanup](ownership-and-lifetime.md) before adding network I/O.
