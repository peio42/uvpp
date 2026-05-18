# Memory, Owners, And Lifetimes

The most important uvpp rule is easy to state: an object that libuv may still use
must stay alive and must stay at the same address.

That sentence hides several concepts. This page breaks them down.

## Owners, Stack, And Heap

An owner is the object responsible for destroying a resource. In C++, an owner
can be an automatic object on the stack, a dynamically allocated object on the
heap, a `std::vector`, a `std::unique_ptr`, or a specialized type.

An object on the stack is destroyed automatically when execution leaves its
scope:

```cpp
int main() {
  uv::loop loop;
  uv::timer timer(loop);

  timer.start(100ms, [](uv::timer& self) {
    self.close();
  });

  loop.run();
  loop.close();
}
```

Here `loop` and `timer` live on the stack in `main`. That is correct because
`loop.run()` executes before the scope exits, and because the timer is closed
during that execution. The `timer` wrapper is not destroyed while libuv can still
call its callback.

An object on the heap is created with `new` and must be destroyed with `delete`:

```cpp
auto* client = new uv::tcp(loop);

client->close([client](uv::tcp&) {
  delete client;
});
```

This pattern is common for dynamically accepted network connections. Closing a
handle is asynchronous: `close()` does not mean "this object can be destroyed
now", but "libuv will close this handle". The close callback tells you that
libuv is done with the native handle.

## Why Handles Are Not Movable

uvpp handles are neither copyable nor movable. That can be surprising when you
come from more value-oriented C++, but it matches libuv's model.

Once a handle is initialized, libuv stores the address of the `uv_timer_t`,
`uv_tcp_t`, and so on. If the wrapper could be moved into another C++ object, the
native handle address could change while libuv still knows the old address. That
would be a lifetime bug.

Do not look for code shaped like this:

```cpp
uv::timer make_timer(uv::loop& loop); // wrong model for an initialized handle
```

The correct shape is to construct the handle directly where it will live:

```cpp
struct service {
  uv::timer retry_timer;

  explicit service(uv::loop& loop)
    : retry_timer(loop) {}
};
```

The `service` itself must also stay stable if its timer is active. The important
point is not only the C++ type: it is the address that libuv may still know.

## Asynchronous Close

A C++ destructor cannot make `uv_close()` synchronous. The low-level uvpp handles
therefore do not close themselves automatically from their destructors.

This program is structurally correct:

```cpp
uv::loop loop;
uv::timer timer(loop);

timer.start(10ms, [](uv::timer& self) {
  self.close();
});

loop.run();
loop.close();
```

This one is dangerous:

```cpp
{
  uv::timer timer(loop);
  timer.start(1s, [](uv::timer&) {});
}

loop.run();
```

The timer is destroyed before the loop has a chance to process the event. libuv
could still reference the native handle. uvpp does not hide that error by trying
to close the handle for the user.

## Requests: One Operation, One Lifetime

A request often represents one operation in progress. It must outlive that
operation.

```cpp
uv::write_request request;
uv::owned_buffer payload{4};
std::memcpy(payload.data(), "ping", 4);

stream.write(request, payload.view(),
  [](uv::write_request&, uv::result status) {
    if (!status) {
      return;
    }
  });
```

Two objects matter here:

- `request` contains the write state and the completion callback.
- `payload` owns the bytes that libuv must read.

Both must stay alive until the callback runs. The `buffer_view` passed to
`write()` is only a view. It does not copy the bytes.

If the request is local to a function that returns immediately, the operation
becomes invalid:

```cpp
void send_ping(uv::tcp& stream) {
  uv::write_request request;
  uv::owned_buffer payload{4};
  std::memcpy(payload.data(), "ping", 4);

  stream.write(request, payload.view(), [](uv::write_request&, uv::result) {});
} // request and payload are destroyed too early
```

One low-level solution is to allocate the operation state on the heap and destroy
it in the callback:

```cpp
struct pending_write {
  uv::write_request request;
  uv::owned_buffer payload;
};

auto* op = new pending_write{
  uv::write_request{},
  uv::owned_buffer{4}
};

std::memcpy(op->payload.data(), "ping", 4);

stream.write(op->request, op->payload.view(),
  [op](uv::write_request&, uv::result status) {
    if (!status) {
      // handle status.error_code()
    }

    delete op;
  });
```

The code is more verbose, but ownership is explicit: `op` owns both the request
and the buffer, and the callback releases that owner when libuv is finished.

## Owned Buffers And Views

uvpp separates storage from views.

```cpp
uv::owned_buffer storage{4096};
uv::buffer_view view = storage.view();
```

`storage` owns a `std::vector<std::byte>`. `view` contains only a pointer and a
size compatible with `uv_buf_t`. Destroying or resizing `storage` invalidates
views produced before that change.

This separation avoids a common ambiguity: one type that sometimes owns memory
and sometimes does not. In uvpp, the type name gives the signal:

- `owned_buffer` owns;
- `buffer_view` borrows;
- `std::span` also borrows, unless another clearly named object owns the bytes.

## `user_data<T>()` Owns Nothing

libuv provides a `void* data` field on handles and requests. uvpp leaves it to
the application and exposes typed helpers:

```cpp
struct session_state {
  int messages = 0;
};

session_state state;
client.user_data(state);

auto* current = client.user_data<session_state>();
current->messages += 1;

client.clear_user_data();
```

The field does not destroy `state`. It does not copy `state`. It also does not
verify that the type requested by the getter is the type that was stored. It is a
convenience around a raw pointer.

This pattern is correct if `state` lives longer than every callback that may use
it. It is incorrect if `state` is a local variable destroyed while the handle is
still active.

## Borrowed Views In Callbacks

Some callback result objects contain views into memory that the result does not
own.

```cpp
client.read_start(allocator, [](uv::tcp&, uv::read_result read) {
  if (read.eof() || !read.ok()) {
    return;
  }

  auto bytes = read.bytes();
  // bytes is a view that is valid during this callback.
});
```

If the bytes must survive the callback, copy them:

```cpp
client.read_start(allocator, [](uv::tcp&, uv::read_result read) {
  if (read.eof() || !read.ok()) {
    return;
  }

  std::vector<std::byte> copy{read.bytes().begin(), read.bytes().end()};
  // copy now owns the bytes.
});
```

The copy has a cost, but it changes the lifetime question: the vector is now the
owner.

