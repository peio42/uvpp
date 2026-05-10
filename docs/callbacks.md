# Callback Strategy

## Goal

uvpp v2 should support both:

- zero-overhead callbacks for users who want libuv-like performance and no allocation;
- ergonomic callables for users who want lambdas with captures.

These should be distinct API paths with clear lifetime behavior.

## Static Callback Mode

Static callback mode uses `template<auto Callback>`.

Example:

```cpp
static void on_tick(uvpp::timer& timer) {
  timer.close();
}

using namespace std::chrono_literals;

uvpp::loop loop;
uvpp::timer timer(loop);

timer.start_static<on_tick>(100ms);
loop.run();
```

Properties:

- no allocation;
- no type erasure;
- no stored callback object;
- each callback instantiation may generate code;
- callback cannot capture runtime state directly.

This mode is appropriate for low-level wrappers and performance-sensitive code.

Use static callbacks when the callback can recover all state from explicit objects, globals, `user_data<T>()`, or protocol state already attached to the handle/request.

Example with explicit user data:

```cpp
struct timer_state {
  int ticks = 0;
};

static void on_counted_tick(uvpp::timer& timer) {
  auto* state = timer.user_data<timer_state>();
  ++state->ticks;

  if (state->ticks == 3) {
    timer.close();
  }
}

using namespace std::chrono_literals;

uvpp::loop loop;
uvpp::timer timer(loop);
timer_state state;

timer.user_data(state);
timer.start_static<on_counted_tick>(100ms, 100ms);
loop.run();
```

This path stores no C++ callable inside the wrapper. The only stored state in this example is the application-owned pointer in libuv's `data` field.

## Runtime Callable Mode

Runtime callable mode accepts lambdas and function objects.

Example:

```cpp
using namespace std::chrono_literals;

uvpp::loop loop;
uvpp::timer timer(loop);

int ticks = 0;

timer.start(100ms, 100ms, [&](uvpp::timer& self) {
  ++ticks;

  if (ticks == 3) {
    self.close();
  }
});

loop.run();
```

Runtime callables require stored state. The default design is:

- trampolines reconstruct the owning C++ wrapper from the native pointer and the wrapper layout invariant;
- each wrapper owns optional callback state for its active callback slots;
- requests own callback state for operations that complete asynchronously.

The implementation must document whether starting a new operation replaces the previous callback for that slot.

Runtime callable mode must not overwrite libuv's `data` field. `data` remains available for user code in both handles and requests.

This path is appropriate for application code where captures make the ownership and control flow clearer than manually wiring state through `user_data<T>()`.

Example with a TCP server connection callback:

```cpp
uvpp::loop loop;
uvpp::tcp server(loop);

int accepted_count = 0;

server.bind(uvpp::ipv4{"0.0.0.0", 2345});
server.listen([&](uvpp::tcp& listener, uvpp::result status) {
  if (!status) {
    return;
  }

  ++accepted_count;

  auto* client = new uvpp::tcp(loop);
  listener.accept(*client);
  listener.close();
  client->close([client](uvpp::tcp&) {
    delete client;
  });
});

loop.run();
```

Here the wrapper stores the runtime callable for the active callback slot. That may use type erasure and storage, but the call site is more natural for code that needs captures.

## Callback Slot Semantics

Low-level wrappers expose one callback slot per active libuv callback family.

Rules:

- `timer.start(...)` replaces the previous timer callback slot before calling `uv_timer_start`;
- `stream.listen(...)` replaces the connection callback slot before calling `uv_listen`;
- `stream.read_start(...)` replaces both allocation and read callback slots before calling `uv_read_start`;
- `handle.close(callback)` replaces the close callback slot and must only be called once for a given handle close lifecycle;
- request callbacks such as `write_request` and `connect_request` are one-shot slots owned by the request object and replaced when submitting a new operation with that request.

Calling a start/listen/read API a second time follows libuv's underlying validity rules. If libuv rejects the operation immediately, uvpp throws `uvpp::error`. If libuv accepts it, the stored callback slot has already been replaced.

After `close()` has been called on a handle, starting new operations on that handle is a logic error. uvpp does not try to recover from it beyond surfacing immediate libuv failures where libuv reports them.

## Trampolines

Every libuv callback should go through a named trampoline in `core/callback.hpp` or a local `detail` namespace.

Example:

```cpp
static void on_timer_raw(uv_timer_t* raw) noexcept {
  auto& self = timer::from_native(raw);
  self.invoke_timer_callback();
}
```

The trampoline is responsible for:

- reconstructing the wrapper reference from the native pointer;
- mapping libuv status values to the selected error policy;
- invoking the user callback;
- handling exceptions according to the exception policy.

## Exception Boundary

C callbacks must not allow exceptions to escape into libuv. Initial v2 has one policy: if a user callback throws, the trampoline calls `std::terminate`.

A future loop-level exception handler can be considered later, but it must be an explicit extension. The first low-level API should not silently store exceptions, stop the loop, or continue after an uncaught callback exception.

## Suggested Callback Signatures

Prefer references over pointers in the C++ API when null is not valid.

```cpp
using timer_callback = void(timer&);
using connection_callback = void(tcp&, result);
using write_callback = void(write_request&, result);
using read_callback = void(stream&, read_result);
```

Use result objects for callbacks that can receive `status`.

## Read Callbacks

Read APIs need careful design because libuv has separate allocation and read callbacks.

Suggested low-level form:

```cpp
stream.read_start(allocator, reader);
```

Suggested higher-level form:

```cpp
stream.read_start(read_buffer_policy::allocate, reader);
```

EOF should not be encoded as a generic exception. It is a normal stream event.

Example result shape:

```cpp
class read_result {
public:
  bool ok() const noexcept;
  bool eof() const noexcept;
  std::span<const std::byte> bytes() const noexcept;
  std::span<char> storage() const noexcept;
  result status() const noexcept;
};
```

`bytes()` represents the payload actually read and is the preferred API for normal processing. `storage()` represents the full buffer returned by the allocation callback; it mainly exists so low-level code can release allocator-owned memory after the read or after a deferred write completes.
