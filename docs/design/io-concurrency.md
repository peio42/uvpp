# Experimental v3 I/O Concurrency

This document records the implemented concurrency contract for the experimental
v3 coroutine owners. It applies to high-level `uv::tcp_connection`,
`uv::pipe_connection`, `uv::udp_socket`, `uv::tcp_listener`, and
`uv::pipe_listener`; it does not extend the separate raw callback API or claim a
complete stable v3 surface.

## Operation slots

Each owner has exclusive operation slots. A slot remains occupied from successful
submission through actual terminal completion. A cancellation request does not
release a slot or borrowed storage early. Terminal completion quiesces the native
source where needed and releases its slot before the awaiting task resumes.

| Resource | Inbound slot | Outbound slot | Permitted overlap |
| --- | --- | --- | --- |
| `tcp_connection` | one `read_some()` | one `write()` | one read and one write |
| `fs::file` | one `read()` | one `write()` | one read and one write |
| `pipe_connection` | one `read_some()` or `receive_handle()` | one `write()` or `write_with_handle()` | one inbound and one outbound operation |
| `udp_socket` | one `recv_from()` | one `send_to()` | one receive and one send |
| `tcp_listener` / `pipe_listener` | one `accept()` | — | — |

Starting a second operation for an occupied slot is an expected operational
failure with `UV_EBUSY`. The experimental member awaits report it as `uv::error`
at the `co_await`. A task on a different loop is a programmer error and reports a
`std::logic_error`, rather than `UV_EBUSY`.

The stream slots are intentionally shared by their IPC variants:
`receive_handle()` conflicts with `read_some()`, and `write_with_handle()`
conflicts with `write()`. This prevents one native read source or one borrowed
write request from acquiring two independent owners. It does not prevent an IPC
read and an IPC write from proceeding together.

The one-outbound-operation restriction is an initial surface limit, not a
permanent architectural limit. It keeps borrowing, cancellation, completion
ordering, and resource-scope cleanup explicit. A future queued or concurrent
writer must specify those properties and can widen this contract without changing
the single-writer programs supported today.

## Closing

`tcp_connection`, `pipe_connection`, and `udp_socket` reject `close()` and
`request_close()` with `UV_EBUSY` while either I/O slot is occupied. `fs::file`
rejects its awaitable `close()` with the same result. A failed close leaves the
active operation and owner state unchanged. Callers must await or otherwise join
that work before closing.

Listeners treat their one active `accept()` differently: close quiesces the
accept, delivers its cancellation after provisional-child cleanup, then starts
native close. Closing a listener stops admission; it does not join application
tasks or close already accepted connections.

## Validation

The coroutine tests cover second same-direction operations returning `UV_EBUSY`,
including `read_some()` versus `receive_handle()` and `write()` versus
`write_with_handle()` on an IPC pipe. They also cover stream and UDP
opposite-direction overlap, listener accept exclusivity, terminal slot release,
cancellation, and active-I/O close rejection. See
[`tests/test-co.cpp`](../../tests/test-co.cpp).

This implementation evidence feeds the remaining operation, buffer, ownership,
and error-policy work in proposals [002](../proposals/002-async-ownership.md),
[004](../proposals/004-operation-state.md), [005](../proposals/005-buffers-and-flow-control.md),
and [006](../proposals/006-errors-and-results.md).
