# Raw stream subscriptions

Include `<uvpp/handles/stream.hpp>`, normally through a concrete handle header.
`uv::raw::stream<Derived, Native>` is the shared caller-owned substrate of
`uv::raw::tcp`, `uv::raw::pipe` and the still-transitional `uv::tty`. There is no
`uv::stream` alias. Native handles remain immovable; native `data` belongs to the
application. High-level TCP/pipe owners have independent storage and APIs.

## Operations and callbacks

| Operation | Submission result | Delivery |
| --- | --- | --- |
| `listen(backlog, callback)` / `listen(callback)` | `uv::status` | `(Derived&, uv::status)` for each connection notification |
| `listen_static<Callback>(backlog = 64)` | `uv::status` | Same arguments, compile-time callable |
| `read_start(allocator, reader)` | `uv::status` | Allocator `(Derived&, size_t) -> buffer_view`; reader `(Derived&, read_result)` |
| `read_start_static<Allocator, Reader>()` | `uv::status` | Same arguments, compile-time callables |
| `read_stop()` | `uv::status` | Stops the subscription; no separate stop callback |
| `accept(client)` | `uv::status` | Uses a separate initialized caller-owned client handle, without a request |
| `blocking(enable)` | `uv::status` | Native blocking-mode switch |

The default backlog is 64. Zero/negative backlogs and empty runtime callables
return `UV_EINVAL`. Uninitialized/closing handles return `UV_EBADF`.
`accept` checks client initialization, closing, loop identity and compatible native
types before calling libuv. A TCP listener accepts into TCP, a normal pipe listener
into pipe, and an IPC pipe into the type reported by `uv_pipe_pending_type`.
No pending connection/IPC handle returns `UV_EAGAIN`. Reading a listener is rejected.
Other native failures retain their native codes; blocking-mode support depends on
the backend (Windows currently supports named pipes, while TCP returns
`UV_EINVAL`). Check submission results, or use `.throw_if_error()` explicitly.

```cpp
uv::raw::tcp peer{loop};
server.listen([&](uv::raw::tcp &listener, uv::status notification) {
  if (!notification) return;
  auto accepted = listener.accept(peer);
  if (!accepted) return;
  // Start a separate read subscription on peer.
}).throw_if_error();
```

Runtime arguments are `std::function`s. Preparing them can allocate or throw before
method entry, leaving native state and existing callbacks unchanged. Native
submission failures use statuses; allocation failures are not translated into
libuv errors. Callback exceptions terminate at the C boundary, for runtime and
static forms. These operations are loop-thread only. The allocator must return
storage without stopping, restarting or closing the stream, replacing its native
callbacks, or recursively running the loop: native backends still access the
stream immediately after allocation returns. Reconfiguration/close from the
reader or connection callback is supported.

## Replacement, stop and close

A successful `listen` replaces the previous runtime/static selection. Failure
retains the previous selection while its native source remains active; if native
listening is inactive the old selection is released. A negative connection
notification retains the listener while native listening remains active (for
example, a failed individual connection). If the backend has stopped listening,
its error is terminal: active slot ownership is released before user delivery. Libuv has no listener-stop operation; close stops listening.

A valid `read_start` on an existing subscription stops the old native read source
before starting the new one. The new pair is installed only on success. If native
stop fails, the old subscription remains; if restart fails after successful stop,
the stream stays stopped and the rejected pair is released. Invalid callbacks are
rejected before stopping the old subscription. Runtime/static replacements share
this protocol. Successful `read_stop` releases active slot ownership and repeated
stops succeed according to libuv. Reentrant replacement, stop and close cannot
restore the executing old callback when it returns; mutable callable state survives
ordinary repeated notifications.

The allocator and reader form one subscription. A buffer already supplied to native
code stays paired with its original reader, even across replacement or stop. That
reader can receive the final native notification for this loan. Both callables
remain alive through that notification; if the backend suppresses notification,
they retire when native code requests its next buffer or completes close. Capturing
a buffer owner in either callable therefore retains it during the loan. External
borrowed storage still belongs to the caller and must survive native use.

`close()`, `close(callback)` and `close_static<Callback>()` stop native delivery and
release active listener/read selection. An outstanding loan remains retained until
native close completion and is released **before** user close delivery. TCP normal
and reset close use the same completion cleanup. A failed reset leaves subscriptions
and pending loans intact. Storage must survive native close completion, including
when no user close callback was supplied. Duplicate/pre-init close is inert.

## Read outcomes and costs

`uv::read_result` keeps the signed native `count()`, borrowed `storage()` and
`raw_buffer()`, and positive-byte `bytes()`. `count() == 0` is an empty notification,
not EOF. `eof()` identifies `UV_EOF`; `ok()`/conversion to bool test nonnegative
counts. `raw_status()`, `error_code()` and `status()` normalize all successful counts
to zero without narrowing large byte counts. EOF remains explicitly identifiable,
with its negative native code in status accessors.

For every negative read notification, including EOF and `UV_ENOBUFS`, the wrapper
stops native reading and releases active slot ownership **before** reader delivery.
The executing allocator/reader pair still retains its buffer until delivery
returns. Restart after EOF/native read error is subject to libuv's native contract;
no recovery or successful restart is promised. Empty/nonterminal notifications
retain the subscription.

Callback pairs and one outstanding native loan are stored inline in the handle.
No callback copy, per-read C++ allocation, payload copy or hidden read queue is
introduced. Runtime callable preparation and buffer allocation by the supplied
allocator remain explicit costs, independent of libuv backend allocations.

## Request operations

[Write, shutdown and TCP connect](raw-requests.md) now use status-oriented
submission and distinct `uv::raw::{write_request,shutdown_request,connect_request}`
objects. Their pending state and callbacks survive until actual request completion,
independently of stream subscription cleanup. `write_now` retains its typed
byte-count / would-block result. [Pipe-specific setup, connect and IPC write](raw-pipe.md) now share these result
contracts. TTY family-specific migration remains a later slice.
