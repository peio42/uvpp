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
static void on_tick(uv::timer& timer) {
  timer.close();
}

using namespace std::chrono_literals;

uv::loop loop;
uv::timer timer(loop);

timer.start_static<on_tick>(100ms);
loop.run();
```

Properties:

- no callback-storage allocation or type erasure for static dispatch;
- no stored callback object;
- each callback instantiation may generate code;
- callback cannot capture runtime state directly.

This is a dispatch guarantee, not a guarantee that the entire operation allocates
nothing. Static DNS and path-based filesystem/watcher calls can allocate string
storage; `getnameinfo_result` owns strings even on the static path. Native libuv
work and user callbacks may also allocate.

Use static callbacks when the callback can recover all state from explicit objects, globals, `user_data<T>()`, or protocol state already attached to the handle/request.

Most handles expose static callback mode as `start_static<Callback>()`, `listen_static<Callback>()`, or an operation-specific equivalent. A few libuv callbacks are fixed when the native object is initialized or spawned. Those wrappers use an explicit tag constructor instead:

Streams and UDP receive expose runtime allocation/read callbacks only. Static
receive adapters are tracked in [proposal 008](../proposals/008-move-only-callbacks.md).

```cpp
uv::async wakeup(loop, uv::async::static_callback<on_wakeup>{});
uv::process child(loop, options, uv::process::static_callback<on_exit>{});
```

The tag form keeps the same zero-overhead property: the wrapper stores no runtime callable for that callback path.

Example with explicit user data:

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

using namespace std::chrono_literals;

uv::loop loop;
uv::timer timer(loop);
timer_state state;

timer.user_data(state);
timer.start_static<on_counted_tick>(100ms, 100ms);
loop.run();
```

This path installs no runtime callable. The wrapper still contains its runtime
callback slots; static invocation does not remove their object-size cost. The
application state in this example is reached through libuv's `data` field.

## Runtime Callable Mode

Runtime callable mode stores copyable lambdas and function objects in
`std::function`. Move-only captured state is not supported by these slots.

Example:

```cpp
using namespace std::chrono_literals;

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
uv::loop loop;
uv::tcp server(loop);

int accepted_count = 0;

server.bind(uv::ipv4{"0.0.0.0", 2345});
server.listen([&](uv::tcp& listener, uv::result status) {
  if (!status) {
    return;
  }

  ++accepted_count;

  auto* client = new uv::tcp(loop);
  listener.accept(*client);
  listener.close();
  client->close([client](uv::tcp&) {
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
- `idle.start(...)`, `prepare.start(...)`, and `check.start(...)` replace their callback slot before calling the corresponding libuv start function;
- `async.set_callback(...)` replaces the runtime callback slot used by an `async` object constructed without a static callback tag;
- `stream.listen(...)` replaces the connection callback slot before calling `uv_listen`;
- `stream.read_start(...)` replaces both allocation and read callback slots before calling `uv_read_start`;
- `udp.receive_start(...)` replaces both allocation and receive callback slots before calling `uv_udp_recv_start`;
- `signal.start(...)` and `signal.start_oneshot(...)` replace the signal callback slot before calling the corresponding libuv function;
- `poll.start(...)` replaces the poll callback slot before calling `uv_poll_start`;
- `fs_event.start(...)` and `fs_poll.start(...)` replace their watcher callback slot before calling the corresponding libuv start function;
- `process` stores its exit callback at construction because `uv_spawn` receives the exit callback when the process is created;
- `handle.close(callback)` replaces the close callback slot and must only be called once for a given handle close lifecycle;
- request callbacks such as `write_request`, `connect_request`, `shutdown_request`, `udp_send_request`, `getaddrinfo_request`, `getnameinfo_request`, and `random_request` are one-shot slots owned by the request object and replaced when submitting a new operation with that request.
- `work_request` owns one worker callback slot and one after-work callback slot. The worker callback runs on a libuv worker thread; the after-work callback runs on the loop thread. Both slots are cleared before or during completion, including cancellation.
- `fs::raw::request` also owns one operation callback slot, but raw FS callbacks must call `req.cleanup()` after consuming the result and before reusing the request.
- `fs` operations own their internal raw request and cleanup it before invoking the public callback with an owned or scalar result.

Calling a start/listen/read API a second time follows libuv's underlying validity rules. If libuv rejects the operation immediately, the wrapper throws `uv::error`. The stored callback slot has already been replaced even if libuv rejects the
start; there is no general rollback to the previous persistent callback.

### Replacing a Callback from Itself

A runtime callback may replace its own persistent slot, for example by calling
`timer.start(...)` from a timer callback. The active callable stays alive until
that invocation returns. If the slot was not replaced, the same callable remains
installed for the next event; if it was replaced, the new callable remains
installed instead. This avoids copying mutable captures while making callback
replacement during invocation safe.

After `close()` has been called on a handle, starting new operations on that handle is a logic error. The wrapper does not try to recover from it beyond surfacing immediate libuv failures where libuv reports them.

Stopping a persistent watcher or switching to a static start generally leaves its
old runtime slot stored. Persistent trampolines invoke that slot in place. V2 does
not provide a safe deferred-replacement mechanism for replacing the executing
callable from inside itself. Request one-shot extraction is a different protocol;
see [proposal 008](../proposals/008-move-only-callbacks.md) for proposed improvements.

## Request `invoke()` Invariants

Runtime request completions use `invoke()` or family-specific methods such as
`work_request::invoke_work()` and `invoke_after()`. Their shared rules are:

1. **Extract and clear the callback slot before calling it.** Use
   `std::move` to take the stored callable, then reset the slot to a default
   state. This is sequencing, not an atomic/thread-safe operation. It permits reuse at
   terminal completion; the worker callback alone is not terminal completion.

2. **Free any owned libuv resources even when the callback slot is empty.** If
   libuv transfers ownership of a heap object to the callback (for example, the
   `addrinfo*` list in a `getaddrinfo` completion), the invoke method must free
   that object when no callback is present to consume it. Raw filesystem requests
   are an explicit exception: cleanup and directory ownership remain the caller's
   protocol, even when no runtime callback is installed.

3. **Be marked `noexcept`.** Exceptions must not propagate through the libuv C
   callback boundary. Any exception thrown by the user callback is caught and
   forwarded to `std::terminate` by `detail::invoke_callback`.

4. **Clear owned submission inputs before invoking user code.** Inputs copied at
   submission time (node name, service name, hints, address storage) must be
   cleared so the request does not retain stale data after completion.

Example of a correct `invoke()`:

```cpp
void invoke(int status, addrinfo *addresses) noexcept {
  auto callback = std::move(callback_);
  callback_ = {};
  clear_inputs();

  if (callback) {
    detail::invoke_callback(callback, *this, result_type{status, addresses});
  } else if (addresses) {
    uv_freeaddrinfo(addresses);  // free even when no callback consumed it
  }
}
```

The static `invoke_static<Callback>()` variant follows the same rules except that
it never stores or clears a runtime callable.

For the submission side, see `detail::submit_request` (4-argument form) and
`detail::submit_with_rollback` in `requests/request.hpp`, which encode these
rollback rules as reusable helpers.

## Trampolines

Native callbacks use static member trampolines, local `detail` functions, or
non-capturing lambdas on static paths. `core/callback.hpp` supplies the shared
exception-boundary invocation helpers.

Example:

```cpp
static void on_timer_raw(uv_timer_t* raw) noexcept {
  auto& self = timer::from_native(raw);
  detail::invoke_callback(self.callback_, self);
}
```

The trampoline is responsible for:

- reconstructing the wrapper reference from the native pointer;
- mapping libuv status values to the selected error policy;
- invoking the user callback;
- handling exceptions according to the exception policy.

## Exception Boundary

C callbacks must not allow exceptions to escape into libuv. V2 has one policy: if a user callback throws, the trampoline calls `std::terminate`.

Alternative exception routing is tracked in the
[errors and results proposal](../proposals/006-errors-and-results.md). The current
low-level API does not store exceptions, stop the loop, or continue after an
uncaught callback exception.

## Callback Argument Shapes

Prefer references over pointers in the C++ API when null is not valid.

```cpp
using timer_callback = void(timer&);
using connection_callback = void(tcp&, result);
using write_callback = void(write_request&, result);
using read_callback = void(tcp&, read_result); // concrete stream wrapper
```

Use result objects for callbacks that can receive `status`.

## Read Callbacks

Read APIs need careful design because libuv has separate allocation and read callbacks.

Current low-level form:

```cpp
stream.read_start(allocator, reader);
```

An owning read adapter is proposed in
[buffers and flow control](../proposals/005-buffers-and-flow-control.md); it is not
part of the current stream read API.

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

The stream `read_result` does not own the buffer. `bytes()`, `storage()`, and `raw_buffer()` are views over the storage returned by the allocation callback. Do not keep those views after the callback unless the backing storage is owned elsewhere and its lifetime is explicitly controlled. Copy the payload when it must survive the callback.

## UDP Receive Callbacks

UDP receive follows the same allocation model as streams. The allocator returns a borrowed `buffer_view`, and `udp_receive_result` exposes views into that storage.

Additional UDP rule: `udp_receive_result::address()` is the source address pointer supplied by libuv and is valid only during the receive callback.

```cpp
udp.receive_start(allocator, [](uv::udp&, uv::udp_receive_result received) {
  if (!received || received.empty_event()) {
    return;
  }

  auto payload = std::vector<std::byte>{received.bytes().begin(), received.bytes().end()};

  if (auto *addr = received.address()) {
    if (addr->sa_family == AF_INET) {
      uv::ipv4 peer{*reinterpret_cast<const sockaddr_in *>(addr)};
      (void)peer; // value copy; may be retained with payload
    } else if (addr->sa_family == AF_INET6) {
      uv::ipv6 peer{*reinterpret_cast<const sockaddr_in6 *>(addr)};
      (void)peer;
    }
  }
});
```

Use `empty_event()` to ignore libuv's zero-length notification events where no source address is available.
