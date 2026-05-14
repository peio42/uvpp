# Getting Started

uvpp is header-only. Add the repository `include/` directory to your compiler include path and link with libuv and pthread.

```sh
g++ -std=c++20 -I/path/to/uvpp/include app.cpp -luv -pthread
```

## CMake Integration

uvpp ships a `CMakeLists.txt` that exposes the `uvpp::uvpp` target. The target sets the C++20 requirement, the include path, and propagates the libuv and pthread dependencies to consumers.

### add_subdirectory or FetchContent

```cmake
# clone or copy uvpp alongside your project, then:
add_subdirectory(uvpp)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE uvpp::uvpp)
```

With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(uvpp
  GIT_REPOSITORY https://github.com/peio42/uvpp.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(uvpp)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE uvpp::uvpp)
```

### find_package (after installation)

After running `cmake --install`, consumers can locate uvpp with:

```cmake
find_package(uvpp REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE uvpp::uvpp)
```

The public API is in namespace `uv`:

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

`uv_close()` is asynchronous. Keep handles alive until their close callback has run, or until the loop has completed all work for stack-allocated handles whose lifetime is already controlled by the surrounding scope.

## Building This Repository

Install libuv and Google Test to build the test suite:

```sh
sudo apt-get update
sudo apt-get install -y g++ libgtest-dev libuv1-dev
```

Run the default suite:

```sh
make test
```

Run the GCC and Clang suites:

```sh
make test-all
```

Build examples:

```sh
make examples
```
