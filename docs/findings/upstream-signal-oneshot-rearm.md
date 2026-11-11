# Rearming a libuv one-shot signal watcher inside its callback

- **Status:** suspected upstream defect or contract ambiguity; behavior reproduced on Linux
- **Component:** `uv_signal_start_oneshot`
- **Affected versions:** runtime: 1.51.0 on Linux; current v1.x Unix and Windows source has analogous post-callback stop; other runtime versions unknown
- **Upstream:** search on 2026-11-11 for libuv signal one-shot callback restart/rearm found no matching issue or fix; search is not proof none exists. Propose reporting the reproduction and asking whether callback rearm is supported.

## Contract and source analysis

The [signal documentation](https://docs.libuv.org/en/v1.x/signal.html) says a one-shot has the same functionality as a normal start but resets the signal handler when the signal is received. It does not explicitly specify whether starting the same handle again inside its callback should survive that callback. A successful start normally suggests an active subscription, but that inference is not an explicit reentrancy guarantee.

In [Unix v1.x signal source](https://github.com/libuv/libuv/blob/v1.x/src/unix/signal.c), a start for the same signum updates the callback and returns success; dispatch invokes the callback and subsequently stops the handle if its one-shot flag is set. The [Windows source](https://github.com/libuv/libuv/blob/v1.x/src/win/signal.c) likewise stops after delivery. Windows has not been tested here.

`include/uvpp/handles/signal.hpp` lines 55–65 and 75–89 use a persistent `uv_signal_start` and explicitly stop before user delivery. This permits a callback to rearm, but also changes native one-shot signal-handler timing and is a workaround for the above native behavior, not merely a storage guard. It should be evaluated against raw native semantics; no implementation change is made here.

## Runtime evidence

On Linux with installed libuv 1.51.0 the standalone program below printed `restart=0 active_in_callback=1` and `active_after_callback=0`. It does not establish Windows behavior, a second delivery, or whether upstream considers rearming supported. It uses `kill`, not `raise`, and never sends a second signal after the handler resets.

```c
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <uv.h>
static uv_signal_t s;
static void cb(uv_signal_t *h, int sig) {
  int rc = uv_signal_start_oneshot(h, cb, sig);
  printf("restart=%d active_in_callback=%d\n", rc, uv_is_active((uv_handle_t*)h));
}
int main(void) {
  uv_loop_t loop; uv_loop_init(&loop); uv_signal_init(&loop, &s);
  uv_signal_start_oneshot(&s, cb, SIGUSR1); kill(getpid(), SIGUSR1);
  uv_run(&loop, UV_RUN_ONCE);
  printf("libuv=%s active_after_callback=%d\n", uv_version_string(),
         uv_is_active((uv_handle_t*)&s));
  uv_close((uv_handle_t*)&s, NULL); uv_run(&loop, UV_RUN_DEFAULT); uv_loop_close(&loop);
}
```

Build on Unix with `cc signal.c -o signal $(pkg-config --cflags --libs libuv) -pthread` and run `./signal`. An upstream report should include the output and request clarification of the callback rearm contract before asserting a defect.
