# Thread Pool Work

`uv::queue_work()` wraps libuv's `uv_queue_work()` request model. It runs one
callback on the libuv worker thread pool and then reports completion on the
event loop thread.

```cpp
uv::loop loop;
uv::work_request req;

int value = 0;

uv::queue_work(loop, req,
  [&](uv::work_request&) {
    value = expensive_blocking_call();
  },
  [&](uv::work_request&, uv::result status) {
    if (!status) {
      return;
    }

    consume(value);
  });

loop.run();
loop.close();
```

The request object must stay alive until the after-work callback runs. This is
true even when cancellation succeeds: libuv still reports cancellation through
the completion callback with `UV_ECANCELED`.

## Thread Safety

The work callback runs outside the loop thread. It must not access `uv::loop`,
handles, or requests except through APIs documented as cross-thread safe by
libuv and uvpp. Use normal synchronization for shared application data.

The after-work callback runs on the loop thread and receives `uv::result`.

```cpp
uv::queue_work(loop, req,
  [](uv::work_request& req) {
    auto* state = req.user_data<job_state>();
    state->run_in_worker();
  },
  [](uv::work_request& req, uv::result status) {
    auto* state = req.user_data<job_state>();

    if (status.canceled()) {
      state->canceled = true;
      return;
    }

    state->done = status.ok();
  });
```

`user_data<T>()` is the native libuv `data` pointer. uvpp does not store wrapper
internals there and does not synchronize access to the pointed object.

## Static Callbacks

Static callbacks avoid runtime callable storage.

```cpp
static void do_work(uv::work_request& req) {
  req.user_data<job_state>()->run_in_worker();
}

static void after_work(uv::work_request& req, uv::result status) {
  req.user_data<job_state>()->done = status.ok();
}

uv::work_request req;
job_state state;

req.user_data(state);
uv::queue_work_static<do_work, after_work>(loop, req);
```

The static work callback still runs in the worker thread, and the static
after-work callback still runs on the loop thread.

## Cancellation

`work_request::cancel()` throws `uv::error` if libuv cannot cancel the pending
request. `work_request::try_cancel()` returns `std::error_code` instead.

Cancellation only succeeds before libuv starts executing the work callback. A
request that has started or already completed cannot be canceled by libuv.
