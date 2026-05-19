# Loop

`uv::loop` owns a `uv_loop_t`. `uv::loop_view` is a non-owning view over an existing loop, such as the default libuv loop.

```cpp
uv::loop loop;
uv::timer timer(loop);

timer.start(100ms, [](uv::timer& self) {
  self.close();
});

loop.run();
loop.close();
```

The loop destructor does not call `uv_loop_close()`. Close the loop explicitly after all associated handles and requests are done.

## Running

`run()` executes pending work and returns `true` when libuv reports that the loop still has active handles or requests after the run mode exits.

```cpp
bool still_alive = loop.run(uv::run_mode::nowait);
```

In the default `uv::run_mode::until_done` mode, libuv runs until there is no more work, so `run()` normally returns `false`.

Use `stop()` to request that the loop stops running.

```cpp
loop.stop();
```

`alive()` reports whether libuv still sees active handles or requests.

## Time

`now()` returns libuv's cached loop time in milliseconds. Call `update_time()` to refresh it before reading if the loop has not just run.

```cpp
loop.update_time();
auto now_ms = loop.now();
```

## Backend Introspection

`backend_fd()` exposes the backend file descriptor when libuv provides one. It is not available on Windows.

`backend_timeout()` returns the timeout the backend would use for the next poll.

```cpp
auto timeout = loop.backend_timeout();

if (!timeout) {
  // infinite wait
} else if (*timeout == std::chrono::milliseconds{0}) {
  // no wait
}
```

The return type is `std::optional<std::chrono::milliseconds>`: `std::nullopt` represents libuv's infinite wait sentinel.

## Metrics

Enable idle time metrics before reading idle time.

```cpp
loop.enable_metrics_idle_time();

// run some work

std::chrono::nanoseconds idle = loop.metrics_idle_time();
```

`metrics_idle_time()` returns a duration. `metrics_info()` returns counters.

```cpp
uv::loop_metrics metrics = loop.metrics_info();

auto iterations = metrics.loop_count;
auto events = metrics.events;
auto waiting = metrics.events_waiting;
```

The metric counters are plain integer counts, not durations.

## Walking Handles

`walk()` visits every handle currently attached to the loop and passes a non-owning `uv::handle_view`.

```cpp
loop.walk([](uv::handle_view handle) {
  if (handle.type() == uv::handle_type::timer && handle.active()) {
    handle.unref();
  }
});
```

`handles()` collects the current walked handles into a vector for range-based loops and ranges pipelines.

```cpp
for (uv::handle_view handle : loop.handles()) {
  if (handle.closing()) {
    continue;
  }
}
```

`handle_view` does not own the handle. Do not keep it past the lifetime of the underlying native handle.

`handle_view::as<T>()` can recover a uvpp wrapper only when the handle is known to have been created by uvpp as that exact wrapper type.

```cpp
loop.walk([](uv::handle_view handle) {
  if (handle.type() == uv::handle_type::timer) {
    uv::timer& timer = handle.as<uv::timer>();
    (void)timer;
  }
});
```

Do not use `as<T>()` for foreign libuv handles or for a different wrapper type.

## Low-Level Loop Operations

`configure_block_signal(signum)` maps to `UV_LOOP_BLOCK_SIGNAL`. On POSIX systems this is commonly used with `SIGPROF`.

```cpp
#ifndef _WIN32
loop.configure_block_signal(SIGPROF);
#endif
```

`fork()` reinitializes a loop after `fork()`. Call it in the child process only.

```cpp
#ifndef _WIN32
pid_t pid = fork();

if (pid == 0) {
  loop.fork();
  loop.run();
  _exit(0);
}
#endif
```
