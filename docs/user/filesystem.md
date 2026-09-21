# Filesystem

The experimental coroutine filesystem slice owns opened descriptors and runs on
the loop inherited by the awaiting task. Include `<uvpp/fs/coroutines.hpp>`.

```cpp
#include <array>
#include <fcntl.h>
#include <uvpp/fs/coroutines.hpp>

uv::co::task<void> copy_once() {
  auto input = co_await uv::fs::open("input.txt", O_RDONLY, 0);
  auto output = co_await uv::fs::open("output.txt", O_CREAT | O_TRUNC | O_WRONLY, 0644);

  std::array<std::byte, 4096> buffer;
  auto read = co_await uv::fs::read(input, buffer, 0);
  if (!read.eof()) {
    auto written = co_await uv::fs::write(output, buffer.first(read.count()), 0);
    (void)written;
  }

  co_await uv::fs::close(input);
  co_await uv::fs::close(output);
}
```

`open(path, flags, mode)` copies its path before submission and returns a
move-only `uv::fs::file`. Its native descriptor is available explicitly through
`file.native()`. A file is bound to the loop on which it was opened. Calling an
operation from a task on another loop is invalid API use and throws
`std::logic_error`.

Opening allocates one stable owner state before the native request is submitted.
The read, write, and close awaiters keep request storage in their coroutine frames.

`native()` borrows the descriptor; it does not transfer close authority. Do not
close it, alter its current position, or start competing asynchronous I/O outside
the file owner.

`read(file, mutable_buffer, offset)` borrows the mutable buffer until its native
completion. It returns `file_read_result`: `count()` is the number of bytes and
`eof()` distinguishes an EOF completion from an empty supplied buffer. `write`
borrows its const buffer for the same interval and returns the completed byte
count. Neither operation loops to fill or drain the buffer, so partial writes
remain visible. An offset of `-1` uses and updates the file position; other
offsets are explicit positions.

One read and one write may overlap on a file. A competing read or write returns
`UV_EBUSY`; `close` also returns `UV_EBUSY` until both directions have settled.
The bytes supplied to a read or write must remain valid, address-stable, and
unchanged until the operation actually completes, including after a stop request.

`close(file)` is cold and non-cancellable. Concurrent close awaiters join one
native `uv_fs_close` request. If request submission fails, the file remains open
and close can be attempted again. Once libuv reports a close completion, the
descriptor is terminal even if that completion reports an error. This prevents a
retry from closing a descriptor number the operating system might have reused.
The owner is then invalid and repeated awaits return that same terminal status.

The destructor diagnoses an unclosed file or active borrowed I/O. Explicitly
close every owner, or transfer it to `uv::co::resource_scope`. A file registration
only yields a `file_view`, which has read/write but no independent close authority.
`resource_scope::finish()` first requires borrowed I/O to have settled, then
awaits close completion. It invalidates the view and releases a terminal file even
when close reports an error; that error is still delivered from `finish()`.

## Explicit results

`uv::ops::fs` uses the same owner and operation state but delivers native
submission and completion failures as results:

```cpp
auto opened = co_await uv::ops::fs::open("input.txt", O_RDONLY, 0);
if (!opened) {
  co_return; // opened.error()
}

auto file = std::move(opened).value();
auto read = co_await uv::ops::fs::read(file, buffer, 0);
auto closed = co_await uv::ops::fs::close(file);
```

The result types are `result<file>`, `result<file_read_result>`,
`result<size_t>`, and `status`, respectively. Allocation while copying an open
path or materializing C++ objects may still throw. A pre-existing cooperative
stop yields `UV_ECANCELED` without submission; once an operation is submitted,
the implementation requests `uv_cancel` and retains the request and borrowed
buffer until libuv's terminal callback.

This slice does not yet include directory owners, scatter/gather filesystem I/O,
copying I/O variants, `read_exactly`, or a queueing policy.
