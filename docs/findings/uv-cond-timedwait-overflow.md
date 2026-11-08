# Large `uv_cond_timedwait` timeouts can expire immediately

- **Status:** confirmed on Linux; Windows limitation suspected
- **Component:** libuv condition variables
- **Affected versions:** Linux runtime observation with 1.51.0 and 1.53.1-dev (commit `2b4b918d`); other versions and Windows runtime behavior unknown
- **Upstream:** no matching report identified in the prior search; not reported by this investigation. Report the native reproducer upstream after checking for an existing issue or fix.

## Observation

On Linux, `uv_cond_timedwait` returned `UV_ETIMEDOUT` within milliseconds for a
`UINT64_MAX` nanosecond timeout without a signal. The expected relative timeout
is roughly 584 years, rather than an immediate timeout.

## Analysis

The reviewed Unix backend adds the relative timeout to a monotonic timestamp in
`uint64_t`, which wraps for this input. Source inspection of the Windows backend
at the same commit shows a conversion to `DWORD` milliseconds that can exceed
the destination range. This is a suspected Windows limitation, not a confirmed
runtime result. See the [Unix](https://github.com/libuv/libuv/blob/2b4b918d/src/unix/thread.c)
and [Windows](https://github.com/libuv/libuv/blob/2b4b918d/src/win/thread.c)
implementations.

uvpp passes the converted timeout to libuv without a retry, native-range clamp,
or fabricated result; see the [threading design](../design/threading-primitives.md).

## Native reproduction

Build the following C program against libuv, for example with
`cc timedwait.c -o timedwait $(pkg-config --cflags --libs libuv) -pthread`, and
run it on Linux. It does not use uvpp. A correct long wait will not finish
promptly; interrupt the program if needed.

```c
#include <stdint.h>
#include <stdio.h>
#include <uv.h>

int main(void) {
  uv_cond_t condition;
  uv_mutex_t mutex;
  uv_cond_init(&condition);
  uv_mutex_init(&mutex);
  uv_mutex_lock(&mutex);
  uint64_t start = uv_hrtime();
  int status = uv_cond_timedwait(&condition, &mutex, UINT64_MAX);
  printf("%s: status=%d, elapsed_ms=%llu\n", uv_version_string(), status,
         (unsigned long long)((uv_hrtime() - start) / 1000000));
  uv_mutex_unlock(&mutex);
  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&condition);
}
```
