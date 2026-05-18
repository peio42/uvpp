# Callbacks, Results, And Errors

uvpp distinguishes two moments where an operation can fail:

- immediate failure while submitting the operation to libuv;
- asynchronous failure reported later in a callback.

This distinction avoids mixing exceptions with completion results.

## Immediate Failure

The primary low-level APIs throw `uv::error` when libuv immediately rejects an
operation.

```cpp
try {
  server.bind(uv::ipv4{"127.0.0.1", 2345});
  server.listen(on_connection);
} catch (const uv::error& e) {
  std::cerr << "submission failed: " << e.what() << '\n';
}
```

An immediate failure means the operation was not accepted by libuv. For example,
a bind can fail because the address is already in use. The operation callback
should not be the primary mechanism for that case: the function has already
failed.

## Completion Failure

When an operation is accepted, it can still fail later. The callback then
receives `uv::result` or a specialized result object.

```cpp
server.listen([](uv::tcp& listener, uv::result status) {
  if (!status) {
    std::cerr << "listen callback: "
              << status.error_code().message()
              << '\n';
    return;
  }

  // A connection is ready to be accepted.
});
```

`uv::result` represents a libuv status. It can be tested as a boolean: true means
success, false means error. Use `error_code()` to obtain a standard C++ error
code.

## Runtime Callbacks

Runtime callbacks are the most natural mode for application code. They accept
lambdas with captures.

```cpp
uv::loop loop;
uv::timer timer(loop);

int ticks = 0;

timer.start(100ms, 100ms, [&](uv::timer& self) {
  ++ticks;

  if (ticks == 3) {
    self.close();
  }
});

loop.run();
loop.close();
```

The wrapper stores the active callback. The `[&]` capture keeps the code compact,
but it does not automatically extend the lifetime of captured variables. Here it
is correct because `ticks`, `timer`, and `loop` remain alive during `loop.run()`.

For a persistent handle, starting a new operation from the same callback family
replaces the callback stored for that family. For example, calling
`timer.start(...)` again installs the new timer callback.

## Static Callbacks

Static callbacks use `template<auto Callback>`. They do not allocate a callable
inside the wrapper and cannot directly capture runtime variables.

```cpp
static void on_tick(uv::timer& timer) {
  timer.close();
}

uv::loop loop;
uv::timer timer(loop);

timer.start_static<on_tick>(250ms);
loop.run();
loop.close();
```

This mode is useful when storing a lambda is not desired, or when the code
follows a model very close to libuv. If a static callback needs state, it must
recover that state from somewhere else.

```cpp
struct timer_state {
  int ticks = 0;
};

static void on_counted_tick(uv::timer& timer) {
  auto* state = timer.user_data<timer_state>();

  ++state->ticks;

  if (state->ticks == 3) {
    timer.close();
  }
}

uv::loop loop;
uv::timer timer(loop);
timer_state state;

timer.user_data(state);
timer.start_static<on_counted_tick>(100ms, 100ms);

loop.run();
loop.close();
```

`user_data` is always non-owning. `state` must therefore remain alive until the
last possible execution of `on_counted_tick`.

## Do Not Let Exceptions Escape Callbacks

libuv callbacks are C callbacks. A C++ exception must not escape across that
boundary. The low-level uvpp policy is strict: if a user callback throws and the
exception reaches the libuv trampoline, the application terminates.

The right practice is to catch exceptions inside the callback when called code
may throw:

```cpp
timer.start(100ms, [&](uv::timer& self) {
  try {
    do_application_work();
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    self.close();
  }
});
```

The callback then decides how to turn the error into application behavior:
closing the handle, logging a message, notifying another component, and so on.

## Typed Results

Not every operation is just success or error. Some return values mix payload and
status.

Stream reads use `uv::read_result`:

```cpp
stream.read_start(allocator, [](uv::tcp& stream, uv::read_result read) {
  if (read.eof()) {
    stream.close();
    return;
  }

  if (!read.ok()) {
    stream.close();
    return;
  }

  std::span<const std::byte> bytes = read.bytes();
  (void)bytes;
});
```

EOF is not treated as an exception. It is a normal branch: the other side closed
the stream cleanly.

Immediate non-blocking operations also use typed results when a special status is
part of normal control flow:

```cpp
auto result = stream.write_now(payload.view());

if (result.would_block()) {
  return;
}

if (result.has_error()) {
  std::cerr << result.error_code().message() << '\n';
  return;
}

std::size_t written = result.bytes_written();
```

The name `write_now()` says that the operation is immediate. It does not own a
request and does not take a callback. If it cannot write right now,
`would_block()` makes that case explicit.

## `try_*` Is Reserved For Non-Throwing Variants

In uvpp, `try_*` does not mean "immediate operation". The prefix is reserved for
variants that do not throw and return an explicit status channel, such as
`std::error_code`.

```cpp
std::error_code ec = loop.try_close();

if (ec) {
  std::cerr << ec.message() << '\n';
}
```

For immediate operations inspired by libuv functions named `try`, uvpp prefers
the `_now` suffix, for example `write_now()`.

