# Callbacks

uvpp exposes two callback styles:

- runtime callbacks for ordinary application code;
- static callbacks without runtime callable storage.

Runtime callback slots use `std::function` and require copyable lambdas and function objects. They can capture state and are stored by the relevant handle or request.

```cpp
using namespace std::chrono_literals;

uv::loop loop;
uv::timer timer(loop);

int ticks = 0;

timer.start(100ms, 100ms, [&](uv::timer& self) {
  if (++ticks == 3) {
    self.close();
  }
});

loop.run();
loop.close();
```

Static callbacks use `template<auto Callback>`. They store no callable in the wrapper and cannot capture runtime state directly.

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

Static dispatch does not allocate callback storage, but the wrapper retains its
runtime slot members. Operations may still allocate inputs or results, such as DNS
strings or filesystem paths. Use `user_data<T>()` or externally owned protocol state
when a static callback needs application state.

```cpp
struct timer_state {
  int ticks = 0;
};

static void on_counted_tick(uv::timer& timer) {
  auto* state = timer.user_data<timer_state>();

  if (++state->ticks == 3) {
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

Callbacks that can fail asynchronously receive `uv::result` or a typed result object. Check the result inside the callback instead of expecting asynchronous errors to throw.

```cpp
server.listen([](uv::tcp& server, uv::result status) {
  if (!status) {
    return;
  }

  // accept a client
});
```

Exceptions must not escape into libuv. If a user callback throws, uvpp treats that as an unrecoverable callback-boundary violation.
