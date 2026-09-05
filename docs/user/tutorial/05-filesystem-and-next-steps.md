# Filesystem And Next Steps

uvpp filesystem operations exist in two layers.

- `uv::fs` is the recommended layer for application code. It owns the internal
  request, cleans libuv state automatically, and returns results that can survive
  the callback.
- `uv::fs::raw` is the layer close to libuv. It exposes the request, requires
  explicit cleanup, and leaves more control to the user.

This separation matters: filesystem operations are a good example of an API
choice where uvpp can offer a more ergonomic layer without changing the
low-level rules.

## Opening A File

```cpp
#include <fcntl.h>
#include <iostream>

#include <uvpp/uv.hpp>

int main() {
  uv::loop loop;

  uv::fs::open(loop, "message.txt", O_RDONLY, 0,
    [&](uv::fs::open_result result) {
      if (!result) {
        std::cerr << result.error_code().message() << '\n';
        return;
      }

      uv::file_descriptor file = result.file();

      uv::fs::close(loop, file,
        [](uv::fs::status_result close) {
          if (!close) {
            std::cerr << close.error_code().message() << '\n';
          }
        });
    });

  loop.run();
  loop.close();
}
```

The callback does not receive a `uv_fs_t`. The `uv::fs` layer created the
internal request, cleaned it, then gave the callback a C++ result.

`file_descriptor` is a thin value around `uv_file`. It does not close the file in
its destructor. Closing a libuv file is also an asynchronous filesystem
operation, so it remains explicit.

## Reading And Closing

The following code opens a file, reads up to 4096 bytes, then closes the
descriptor.

```cpp
uv::fs::open(loop, "message.txt", O_RDONLY, 0,
  [&](uv::fs::open_result open) {
    if (!open) {
      std::cerr << open.error_code().message() << '\n';
      return;
    }

    uv::file_descriptor file = open.file();

    uv::fs::read(loop, file, 4096, 0,
      [&, file](uv::fs::read_result read) {
        if (read) {
          std::span<const std::byte> bytes = read.bytes();
          (void)bytes;
        } else {
          std::cerr << read.error_code().message() << '\n';
        }

        uv::fs::close(loop, file,
          [](uv::fs::status_result close) {
            if (!close) {
              std::cerr << close.error_code().message() << '\n';
            }
          });
      });
  });
```

`uv::fs::read_result` owns its buffer. That differs from `uv::read_result` for
streams, where bytes are a view into the buffer provided by the allocator. In
the public filesystem layer, the result can be kept or copied later according to
the application's needs.

## Writing Bytes

`uv::fs::write` copies submitted bytes into storage owned by the operation. The
local array may therefore disappear after submission.

```cpp
std::array<char, 2> payload{'o', 'k'};

uv::fs::write(loop, file, std::as_bytes(std::span{payload}), 0,
  [](uv::fs::byte_count_result result) {
    if (!result) {
      std::cerr << result.error_code().message() << '\n';
      return;
    }

    std::size_t written = result.count();
    (void)written;
  });
```

This behavior is more convenient than the raw API, but it has a cost: the public
layer owns and copies operation state to make lifetime management simpler.

## When To Use `uv::fs::raw`

The raw layer is useful when you want to:

- explicitly reuse a request;
- avoid copies;
- follow the exact libuv shape;
- use static callbacks;
- control request-scoped buffers and results.

```cpp
uv::fs::raw::request req;

uv::fs::raw::open(loop, req, "message.txt", O_RDONLY, 0,
  [&](uv::fs::raw::request& req, uv::fs::raw::open_result result) {
    auto cleanup = req.scoped_cleanup();

    if (!result) {
      std::cerr << result.error_code().message() << '\n';
      return;
    }

    uv::file_descriptor file = result.file();
    (void)file;
  });
```

`scoped_cleanup()` calls `uv_fs_req_cleanup()` at the end of the scope. Views
that point into the raw request become invalid after this cleanup. If a value
must survive, copy it first.

Like all low-level requests, `req` must remain alive until the callback runs. If
this operation is started from a function that returns before `loop.run()`, put
the request in an owner that lives long enough.

## Reading The Reference Documentation

The tutorial gives the general mental model. The reference pages in `docs/user/` then
give the details for each API family:

- [Loop](../loop.md)
- [Callbacks](../callbacks.md)
- [Errors](../errors.md)
- [Ownership and lifetime](../ownership-and-lifetime.md)
- [Buffers](../buffers.md)
- [Streams](../streams.md)
- [Filesystem](../filesystem.md)
- [Process](../process.md)

To contribute to the public API shape, also read the design documents in
[docs/design](../../design/). They explain why uvpp keeps native conversions
explicit, why views are produced through `.view()`, and why low-level handles are
neither copyable nor movable.

