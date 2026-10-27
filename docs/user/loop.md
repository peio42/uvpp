# Loop

`uv::loop` owns a `uv_loop_t`. `uv::loop_view` is a non-owning view over an existing loop, such as the default libuv loop.

All v3 layers share this loop. See the complete [startup example](getting-started.md).
A task or owner borrows the loop; none creates a second scheduler implicitly.

The loop destructor does not call `uv_loop_close()`. Close the loop explicitly after all associated handles and requests are done.

## Running

`run()` executes pending work and returns `true` when libuv reports that the loop still has active handles or requests after the run mode exits.

```cpp
bool still_alive = loop.run(uv::run_mode::nowait);
```

In the default `uv::run_mode::until_done` mode, libuv runs until there is no more work, so `run()` normally returns `false`.

Use `stop()` to request that the loop stops running. This does not cancel tasks,
close owners, or complete native cleanup; resume driving the loop as needed.

```cpp
loop.stop();
```

`alive()` reports whether libuv still sees active handles or requests.

## Cross-thread posting

Include `<uvpp/loop_posting.hpp>`. `uv::loop_posting` is an experimental explicit
producer-to-loop bridge. Create
its owner on the loop thread and pass an endpoint to producer threads. A posted
callable runs later on the loop thread.

```cpp
uv::loop_posting posting(loop);
auto endpoint = posting.endpoint();

std::thread producer([endpoint] {
  endpoint.post([] {
    // Runs on the loop thread.
  });
});
producer.join();

posting.request_close(); // rejects new posts; accepted work will drain
loop.run();              // completes the drain and native close
loop.close();
```

The queue is bounded. `endpoint.post()` throws for a full or closed component;
`uv::ops::post(endpoint, callable)` returns `uv::status` instead. Accepted work
runs once if the application continues to drive the loop through close completion.
The owner must remain alive until then. Callables may be move-only; their queue
storage may allocate. A [complete example](../../examples/loop-posting.cpp) shows
move-only payloads and explicit submission results.

The constructor accepts `(loop, capacity = 1024, drain_budget = 64)`. Both counts
must be nonzero. Capacity counts waiting callables; a batch of up to
`min(capacity, drain_budget)` additional callables may already be executing. A full
queue reports `UV_EAGAIN`; admission closed by `request_close()` reports
`UV_ECANCELED`. Submission never waits for capacity, but may contend on a mutex.
Both surfaces may throw for allocation or callable construction before acceptance.
On rejection the prepared callable is destroyed on the submitting thread, outside
the queue lock. Its captures may already have been moved from the caller.

Posting from the loop thread also queues work; it never executes inline. Sequential
accepted posts from a producer retain their order. Concurrent producers have no
wall-clock ordering guarantee. The drain returns to libuv between batches, but a
long-running individual callable can still delay I/O. The wakeup handle is
referenced: an open component keeps `loop.run()` alive even with an empty queue.

All owner methods, including handler installation, closing, and destruction, are
loop-thread operations. Copy endpoints to producer threads; submitting through
stable endpoints is thread-safe, while concurrently modifying or destroying the
same endpoint object is not. Endpoints retain posting state rather than the loop.
After close they can outlive the owner and loop and continue returning rejection.
Captured references and pointers retain their own lifetime obligations; queued
work does not extend external borrowed storage.

`request_close()` initiates shutdown immediately and is idempotent. The phase is
sealed before draining: a callable or capture destructor that posts again receives
rejection. `closed()` becomes true at native close completion. Destroying the owner
before that point terminates. Do not close its internal handle through a walked
native view; its callbacks and `data` are reserved by uvpp.

Within a task spawned on this loop, `co_await posting.close()` initiates or joins
the same shutdown. Multiple waiters are supported, and completed close is
idempotent. Constructing the awaiter alone does not seal admission. A different
task loop throws `std::logic_error`, including for an already closed component.
Registering a waiter may allocate; close does not become cancellable when the
task receives a stop request. Close completion settles the callables themselves,
not asynchronous operations they may have started.

Exceptions in posted callables are caught on the loop thread. Install a component
handler before delivery with `posting.set_failure_handler(handler)`, where the
handler accepts `std::exception_ptr`. It may replace itself or request close;
replacement applies to the next failure. A returning handler allows the remaining
accepted work to drain. An absent or throwing handler terminates. This policy is
separate from the loop's unobserved root-task failure handler. Handler captures are
released at close completion, even if producer endpoints survive.

## Unobserved root failures

An exception is observed when `spawn_handle::rethrow_if_failed()` or
`take_result()` is called after root completion. `join()` is only a completion
barrier and does not observe an exception. If a failed root loses its public
handle without observation, the loop routes that exception to its unobserved
failure handler exactly once.

The default policy is `std::terminate()`. Install a handler before the failure
is routed to log or supervise it:

```cpp
loop.set_unobserved_failure_handler([](std::exception_ptr failure) {
  // Record failure. Under the current loop-thread-only spawn-handle contract,
  // this runs on the loop thread.
  (void)failure;
});
```

Installing the handler may allocate. The handler must not throw; an escaping
handler exception terminates. Passing an empty `uv::loop::unobserved_failure_handler`
restores the default terminating policy.

A handler may replace or clear itself while it runs. The current invocation keeps
its original callable alive; the replacement applies to the next unobserved
failure.

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

Walking exposes native handles, including handles used internally by high-level
owners. Do not close them or replace their callbacks through a view: use the
owning object's API. Do not recover a low-level wrapper from a high-level owner's
native handle; their storage representations differ.

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
