# Raw random bytes

`uv::raw::random_fill_now(buffer)` fills a caller-owned `std::span<std::byte>`
synchronously and returns `uv::status`. It needs neither a loop nor request and
may block while the system obtains entropy. Libuv fills the entire buffer on
success; on error its contents are unspecified. The wrapper passes native flags
as zero and does not allocate an output buffer.

For asynchronous operation, the caller supplies both stable request storage and a
mutable output buffer:

```cpp
#include <array>
#include <uvpp/misc/random.hpp>

uv::loop loop;
uv::raw::random_request request;
std::array<std::byte, 32> bytes{};
auto submitted = uv::raw::random_fill(loop, request, bytes,
  [](uv::raw::random_request&, uv::raw::random_result result) {
    if (result) { /* use result.bytes() */ }
  });
if (submitted) loop.run();
```

`random_fill_static<Callback>` uses a compile-time callback. Runtime callback
construction may allocate and throw before submission; native errors are
returned as statuses. An empty runtime callback is rejected with `UV_EINVAL`:
passing a null callback to libuv selects synchronous execution. Both asynchronous
forms use caller-owned, address-stable `random_request` storage and borrow the
output buffer; uvpp does not own or copy it. Keep both alive and do not access,
resize, or free the buffer until completion. The caller retains ownership after
completion. This API is available when `UVPP_HAS_RANDOM` is enabled (libuv 1.33+).

The callback runs on the loop thread with a `raw::random_result`: `status()`,
`raw_status()` and `error_code()` expose native completion; `buffer()` borrows the
original output range on success or failure, and `bytes()` exposes it only on
success. A failed result does not imply valid output bytes. The caller controls
the lifetime of either borrowed span. Completion releases the stored callback and
clears `pending()` before delivery, permitting reuse or destruction of the request
from the callback. A new operation must not overlap access to an earlier buffer.
Exceptions cannot escape the C callback and terminate the process.

`pending()` remains true until completion and a second submission returns
`UV_EBUSY`. `cancel()` returns native cancellation status while pending and
`UV_EINVAL` otherwise. Cancellation is a request, not completion: it may fail if
work already started, and even a successful cancellation requires retaining the
request and buffer until the callback. An accepted cancellation is delivered as
`UV_ECANCELED`. Request methods are loop-thread-affine. Libuv's asynchronous
random operation uses the shared thread pool and may be delayed indefinitely on a
system short of entropy. See [libuv random documentation](https://docs.libuv.org/en/v1.x/misc.html#c.uv_random).
