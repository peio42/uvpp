# Raw thread-pool work

`uv::raw::work_request` owns caller-provided, address-stable `uv_work_t` storage.
`uv::raw::queue_work` submits a runtime work callback and an optional after-work
callback; `queue_work_static<Work, After>` uses compile-time callbacks. Both return
`uv::status` for submission. Runtime callback construction can allocate and throw
before the operation is submitted; no additional operation allocation is made by
uvpp. Libuv may allocate or initialize its own thread pool.

```cpp
#include <uvpp/threadpool/work.hpp>

uv::loop loop;
uv::raw::work_request request;
auto submitted = uv::raw::queue_work(loop, request,
  [](uv::raw::work_request&) { /* worker thread */ },
  [](uv::raw::work_request&, uv::status completion) { /* loop thread */ });
if (submitted) loop.run();
```

The work callback executes in libuv's shared thread pool and may begin before
`queue_work` returns. The completion callback executes on the submitting loop's
thread. Exceptions must not escape either callback; uvpp terminates on an exception.
Application state shared across threads requires application synchronization.
Do not access loop-owned state from the worker except through an entry point that
explicitly permits it. The application's `uv_req_t::data` remains available; its
access and pointee are not synchronized by uvpp.

The request and all state borrowed by either callback must stay alive and at a
stable address until completion is delivered. `pending()` stays true through
worker execution and until immediately before completion delivery. Submission,
`pending()` and `cancel()` are loop-thread operations. Submitting a pending
request returns `UV_EBUSY`; an empty work callback returns `UV_EINVAL`. The
runtime after-work callback may be empty: uvpp still installs an internal native
completion callback to retire the request and release its callback storage. Drive
the loop until `pending()` is false before reusing or destroying the request. A
native submission error returns its native status and releases both stored callbacks.

`request.cancel()` returns the native cancellation status while pending and
`UV_EINVAL` otherwise. A successful cancellation does not complete the request:
the work callback will not run, but native completion still occurs; if supplied,
the user completion callback receives `UV_ECANCELED`. If work has begun or
finished, cancellation can return `UV_EBUSY`; any user completion callback still
reports the eventual outcome. Never destroy or
reuse the request merely because cancellation was requested. Once native completion
has cleared `pending()`, the request may be resubmitted or destroyed; a user
completion callback may do so from within the callback.

The work callable is moved out and destroyed on the worker after invocation; if
cancelled before it starts, it is released on the loop before completion delivery.
The completion callable is also released from the request before delivery, and
the local callable is destroyed after invocation on the loop thread. Its captures must
remain valid through that call, including when the request is reused in it.
