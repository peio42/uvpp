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

Buffer lengths must fit the platform's `uv_buf_t::len` field. Preparing either
`read` or `write` throws `std::length_error` for an unrepresentable length,
including through `uv::ops::fs`; no native request is submitted. Successful byte
counts retain their full native width.

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

The coroutine slice does not yet include directory owners, scatter/gather
filesystem I/O, copying I/O variants, `read_exactly`, or a queueing policy.

## Callback filesystem I/O

The callback facade in `<uvpp/fs/operations.hpp>` also borrows caller storage by
default. `read` fills the supplied mutable span and `write` reads from the
supplied const span. Keep both buffers alive, address-stable, and unmodified
until their completion callback runs.

```cpp
std::array<std::byte, 4096> buffer;

uv::fs::read(loop, file, buffer, 0, [](uv::fs::byte_count_result result) {
  if (result) {
    // The caller-owned buffer contains result.count() bytes.
  }
});
```

`write_copy` explicitly retains a copy of source bytes until completion.
`read_owned` allocates and returns an `owned_buffer` in `read_result`. Use these
forms when the callback caller cannot keep its own storage alive for the
operation.

## Raw filesystem protocol

Include `<uvpp/fs/operations.hpp>` for `uv::raw::fs`. It contains the complete
caller-controlled filesystem protocol: `request`, typed completion results,
`directory`, `directory_read_buffer`, flags, and operations. There is no
`uv::fs::raw` compatibility alias. The shared loop and descriptor/buffer vocabulary
remain in `uv`.

Every operation returns an immediate `uv::status`. Success means submission was
accepted; its completion result arrives through the callback. Failure means no
completion will arrive and the runtime callback has been cleared. Clean the
request after either native completion or immediate native rejection, before reuse.
A request is neither copyable nor movable, and its native `data` remains available
to application code.

```cpp
uv::raw::fs::request request;
auto submitted = uv::raw::fs::stat(loop, request, "input.txt",
  [](uv::raw::fs::request& request, uv::raw::fs::stat_result result) {
    auto cleanup = request.scoped_cleanup();
    if (result) {
      // Consume result.native() while request storage is still valid.
    }
  });
if (!submitted) {
  request.cleanup();
  // Inspect submitted.error(). No callback is pending.
}
loop.run();
```

Supply a valid callback. The `operation_static<Callback>(...)` forms use a
compile-time callback with the same arguments and submission status. Runtime
one-shot callbacks are removed before delivery, allowing cleanup and resubmission
from inside the callback. Exceptions escaping either callback form terminate.
Neither form automatically cleans the native request.

Results preserve descriptors, counts, and errors through typed accessors;
`raw()` retains the native result, and `status()` normalizes successful payloads
to zero. A raw read returning zero bytes follows libuv semantics; callers must
account for an empty input buffer before interpreting it as EOF. Partial progress
is not retried. Read/write buffers must survive actual completion, even after a
native cancellation request. Path/stat/statfs/scandir results borrow native request
storage and must be consumed or copied before cleanup. Scandir iteration is
single-pass. Directory entry names from `readdir` also expire at request cleanup;
consume them before cleaning that request or starting another read.

Successful `opendir_result` must be consumed with `take_directory()`. Request
cleanup does not close the directory. Keep the directory and its read buffer
stable through each pending `readdir`, and clean that request before `closedir`.
Pass the directory to `closedir` with `std::move`; ownership is released only if
submission succeeds. On rejection the directory remains yours to retry; on
accepted submission its storage belongs to libuv through completion. Directory
and unconsumed open-directory result destructors diagnose abandoned ownership
with assertions; they do not close implicitly. File descriptors returned by raw
`open` likewise need an explicit close.

Raw filesystem calls allocate no hidden uvpp operation state. Runtime callbacks
use `std::function` and may allocate; path arguments are temporarily copied for
NUL termination, directory read buffers allocate their entry array, and libuv may
allocate native request storage. These setup costs may throw C++ exceptions;
submission is not generally `noexcept`. The high-level callback facade allocates
owned operation state, cleans rejected native requests, and throws on immediate
native failure. Its typed completion and buffer policies are unchanged.
