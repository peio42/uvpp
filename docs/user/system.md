# System Utilities

uvpp exposes small wrappers for libuv's cross-platform system and process
utility functions. Include the aggregate header:

```cpp
#include <uvpp/uv.hpp>
```

## Environment

Environment helpers copy names and values into null-terminated storage before
calling libuv.

```cpp
uv::set_environment_variable("UVPP_MODE", "test");

if (auto value = uv::environment_variable("UVPP_MODE")) {
  // use *value
}

uv::unset_environment_variable("UVPP_MODE");
```

`environment_variable()` returns `std::nullopt` when the variable is absent.
Other immediate libuv failures throw `uv::error`.

## Process

```cpp
uv::process_id current = uv::pid();
uv::process_id parent = uv::parent_pid();

int priority = uv::process_priority(current);
```

Priority values are platform-specific integers. On Unix-like systems they
normally follow the usual nice range where lower values mean higher priority.
Setting process priority may require OS privileges:

```cpp
uv::set_process_priority(current, priority);
```

## System Information

```cpp
std::string host = uv::hostname();
uv::uname_info os = uv::uname();

auto cpus = uv::cpu_infos();
auto interfaces = uv::interface_addresses();
auto load = uv::load_average();
auto up = uv::uptime();

std::uint64_t total = uv::total_memory();
std::uint64_t free = uv::free_memory();
std::uint64_t rss = uv::resident_set_memory();
```

`cpu_infos()` and `interface_addresses()` return owned C++ values. uvpp frees
the temporary libuv arrays before returning to the caller.

`uptime()` returns `std::chrono::duration<double>`.

When available in the linked libuv headers, `available_parallelism()` returns
libuv's estimate of useful default parallelism:

```cpp
#if UVPP_HAS_AVAILABLE_PARALLELISM
unsigned int workers = uv::available_parallelism();
#endif
```

## Paths

```cpp
std::string temporary = uv::tmpdir();
std::string home = uv::homedir();
std::string here = uv::cwd();
std::string exe = uv::exepath();

uv::chdir(temporary);
```

`chdir()` changes the process-wide current working directory.

## Blocking Sleep

`uv::sleep_blocking_for()` wraps `uv_sleep()` and blocks the current thread:

```cpp
using namespace std::chrono_literals;

uv::sleep_blocking_for(10ms);
```

Do not use it as an event-loop delay. If it runs on the thread expected to call
`loop.run()`, that loop cannot dispatch timers, I/O, or callbacks until the
sleep returns. The experimental coroutine counterpart is
`uv::co::sleep_for(duration)`: it inherits its spawned task's loop and uses a
timer handle. See [Experimental coroutines](coroutines.md); its final v3 API is
not yet settled.
