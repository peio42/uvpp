# Random Bytes

`uv::random_fill()` wraps libuv's `uv_random()` for cryptographically strong
random bytes from the operating system CSPRNG.

## Synchronous Fill

The synchronous overload fills caller-owned storage and throws `uv::error` on
failure.

```cpp
std::array<std::byte, 32> token{};

uv::random_fill(std::span<std::byte>{token});
```

Use `try_random_fill()` when immediate failure should be reported without an
exception.

```cpp
auto ec = uv::try_random_fill(std::span<std::byte>{token});
if (ec) {
  return;
}
```

The synchronous form may block while the system waits for entropy.

## Asynchronous Fill

The asynchronous overload uses `uv::random_request`.

```cpp
uv::loop loop;
uv::random_request req;
std::array<std::byte, 32> token{};

uv::random_fill(loop, req, std::span<std::byte>{token},
  [&](uv::random_request&, uv::random_result result) {
    if (!result) {
      return;
    }

    use_token(result.bytes());
  });

loop.run();
loop.close();
```

The request and the byte storage must both stay alive until the callback runs.
`random_result::bytes()` is a borrowed span over the same storage passed to
`random_fill()`. Copy the bytes if they need to outlive that storage.

`random_result` exposes the standard result-object shape:

```cpp
bool ok = result.ok();
uv::result status = result.status();
int raw = result.raw_status();
std::error_code ec = result.error_code();
```

Short reads are not reported as partial success by libuv. On success, the full
span has been filled. On failure, the contents of the buffer are undefined.

## Static Callbacks

```cpp
static void on_random(uv::random_request&, uv::random_result result) {
  if (!result) {
    return;
  }

  use_token(result.bytes());
}

uv::random_fill_static<on_random>(loop, req, std::span<std::byte>{token});
```

## Availability

`uv_random()` was added in libuv 1.33.0. The wrapper is available when
`UVPP_HAS_RANDOM` is true.
