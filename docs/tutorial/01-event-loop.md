# Understanding The Execution Model

uvpp exposes its public API in the `uv` namespace and is normally used through
the aggregate header:

```cpp
#include <uvpp/uv.hpp>
```

The project is named `uvpp`, the include directory is named `uvpp`, but user code
writes `uv::loop`, `uv::timer`, `uv::tcp`, and so on.

## The Event Loop

A libuv application does not start an asynchronous operation and then wait for
the result through an ordinary function return. It registers work with an event
loop. The loop watches timers, sockets, processes, files, and signals, then calls
callbacks when something happens.

In uvpp, `uv::loop` owns a native `uv_loop_t`:

```cpp
#include <chrono>
#include <iostream>

#include <uvpp/uv.hpp>

using namespace std::chrono_literals;

int main() {
  uv::loop loop;

  uv::timer timer(loop);
  timer.start(250ms, [](uv::timer& self) {
    std::cout << "timer expired\n";
    self.close();
  });

  loop.run();
  loop.close();
}
```

This program already contains the full cycle:

- `uv::loop loop;` initializes a native loop.
- `uv::timer timer(loop);` initializes a timer handle attached to that loop.
- `timer.start(...)` submits an operation to libuv. The function returns
  immediately after submission.
- `loop.run();` gives control to libuv. The timer callback runs only during this
  call.
- `self.close();` requests handle closure.
- `loop.close();` closes the loop once there are no active handles left.

The callback receives a reference to the timer, not a nullable pointer. That
means uvpp treats the timer as present and valid during the callback call.

## A Repeating Timer

A timer can receive an initial delay and a repeat interval. Public duration
values use `std::chrono`, so ordinary code does not need to pass bare millisecond
counts.

```cpp
#include <chrono>
#include <iostream>

#include <uvpp/uv.hpp>

using namespace std::chrono_literals;

int main() {
  uv::loop loop;
  uv::timer timer(loop);

  int ticks = 0;

  timer.start(100ms, 500ms, [&](uv::timer& self) {
    ++ticks;
    std::cout << "tick " << ticks << '\n';

    if (ticks == 3) {
      self.close();
    }
  });

  loop.run();
  loop.close();
}
```

The first `100ms` is the delay before the first callback. The second `500ms` is
the repeat interval. The `ticks` counter lives on the stack in `main`, and the
callback captures it by reference. That capture is correct here because `main`
stays on the stack while `loop.run()` is executing, and because the timer is
closed before `main` leaves its scope.

If the timer outlived the scope that contains `ticks`, the captured reference
would become invalid. uvpp cannot detect that kind of mistake: the callback is
application code, and the application must ensure captured objects live long
enough.

## Handles And Requests

libuv uses two major families of objects.

A handle represents a resource that can remain attached to the loop:

- timer;
- TCP socket;
- pipe;
- signal;
- filesystem watcher;
- process.

In uvpp, these objects are wrappers such as `uv::timer`, `uv::tcp`, or
`uv::fs_event`. A handle is initialized with a loop and can receive persistent or
repeating operations.

A request represents one specific asynchronous operation:

- a TCP connection in progress;
- a write on a stream;
- a stream shutdown;
- a raw filesystem operation.

In uvpp, these are objects such as `uv::connect_request`, `uv::write_request`, or
`uv::shutdown_request`. A request must stay alive until the completion callback
of the operation that uses it.

```cpp
uv::write_request request;

stream.write(request, buffer.view(),
  [](uv::write_request& req, uv::result result) {
    (void)req;
    if (!result) {
      return;
    }
  });
```

In this example, `stream.write(...)` does not mean the bytes have already been
sent when the function returns. It means the write has been submitted. The
request and the buffer must therefore stay alive until the callback runs.

## `run()` Does Not Create A Thread

`loop.run()` executes the loop on the current thread. uvpp does not create a
hidden thread. While `loop.run()` is running, that thread calls callbacks and
processes events. When no active work remains, the default `UV_RUN_DEFAULT` mode
lets `run()` return.

```cpp
bool still_alive = loop.run(UV_RUN_NOWAIT);
```

With `UV_RUN_NOWAIT`, `run()` performs one non-blocking iteration. Its return
value indicates whether libuv still sees active work after that iteration.

This distinction is useful when integrating uvpp into a program that already has
its own main loop. For a simple application, the default mode is still the common
choice.

## Referenced And Unreferenced Handles

An active handle normally keeps the loop alive. The `ref()` and `unref()`
functions change that relationship.

```cpp
uv::loop loop;
uv::timer heartbeat(loop);

heartbeat.start(1s, 1s, [](uv::timer&) {
  // periodic work
});

heartbeat.unref();

loop.run();
loop.close();
```

Here the timer is active, but it is no longer enough to keep the loop open. If no
other referenced handle is active, `loop.run()` may return. This is useful for
background work that should not prevent the application from exiting.

The handle still exists, and it must still remain alive while libuv may use it.
`unref()` does not transfer ownership and does not destroy anything.

## Borrowed Views

uvpp sometimes exposes non-owning views. For example, `timer.view()` produces a
`uv::handle_view`:

```cpp
uv::handle_view handle = timer.view();

if (handle.active()) {
  handle.unref();
}
```

A view is not an owner. It does not close the handle and does not extend its
lifetime. It is only a typed pointer to a resource owned somewhere else. The
`.view()` name is intentional: code that reads it should see that the returned
value borrows something.

The same idea appears with buffers: `owned_buffer` owns bytes, while
`buffer_view` borrows them.

