# Filesystem

The filesystem API is split into two layers:

- `uv::fs`: the recommended C++ API. It owns the internal `uv_fs_t`, performs `uv_fs_req_cleanup()` automatically, and returns values that can safely outlive the callback.
- `uv::fs::raw`: the direct libuv-shaped API. It exposes `uv_fs_t` through `raw::request`, requires explicit cleanup, and keeps request-owned views request-scoped.

This split keeps the public API aligned with the rest of v2: lifetime rules are encoded by default, while the low-level layer remains available when the caller wants exact libuv control or zero-extra-copy behavior.

## `std::filesystem::path` Interoperability

Use `uv::fs::path_argument()` when an application holds a
`std::filesystem::path` and needs to submit it to a libuv filesystem
operation:

```cpp
std::filesystem::path path = "assets/logo.svg";
uv::fs::stat(loop, uv::fs::path_argument(path), callback);
```

The function returns an owned string. On Windows it converts the native wide
path to UTF-8, which is the path encoding expected by libuv. On POSIX it
preserves the native byte representation: filesystem names there are not
necessarily valid UTF-8.

It rejects embedded NUL characters. It performs no filesystem access,
normalization, canonicalization, or security validation; callers remain
responsible for those concerns.

## Choosing a Layer

Use `uv::fs` by default. Switch to `uv::fs::raw` only when you need one of the following:

| Requirement | `uv::fs` | `uv::fs::raw` |
|---|---|---|
| Automatic cleanup after callback | ✅ | ❌ Manual `req.cleanup()` |
| Results safe to copy or keep after callback | ✅ | ⚠️ Views expire at cleanup |
| Reuse the same request across operations | ❌ Hidden, internal | ✅ Caller-owned `raw::request` |
| Zero-copy read / write (caller-owned buffer) | ❌ Copies into owned buffer | ✅ |
| Static callbacks with no heap allocation | ❌ | ✅ `*_static<Callback>()` |
| `opendir` / `readdir` / `closedir` | ❌ Not available | ✅ |

## Public API

`uv::fs` allocates one internal raw request per operation and destroys it after the callback has received an owned or scalar result.

```cpp
uv::fs::open(loop, "file.txt", O_RDONLY, 0,
  [&](uv::fs::open_result result) {
    if (!result) {
      return;
    }

    uv::file_descriptor file = result.file();
  });
```

The callback does not receive a request. There is no public cleanup step.

The public API includes:

- `fs::open`
- `fs::close`
- `fs::read`
- `fs::write`
- `fs::stat`
- `fs::unlink`
- `fs::rename`
- `fs::mkdir`
- `fs::mkdtemp`
- `fs::mkstemp` when `UVPP_HAS_FS_MKSTEMP` is available
- `fs::rmdir`
- `fs::fstat`
- `fs::lstat`
- `fs::statfs` when `UVPP_HAS_FS_STATFS` is available
- `fs::access`
- `fs::chmod`
- `fs::fchmod`
- `fs::chown`
- `fs::fchown`
- `fs::lchown`
- `fs::utime`
- `fs::futime`
- `fs::lutime`
- `fs::fsync`
- `fs::fdatasync`
- `fs::ftruncate`
- `fs::link`
- `fs::symlink`
- `fs::realpath`
- `fs::readlink`
- `fs::copyfile`
- `fs::sendfile`
- `fs::scandir`

`opendir`, `readdir`, and `closedir` remain raw-only for now because they expose an incremental directory cursor and caller-provided entry buffers. A higher-level directory API can be added later without changing the raw layer.

## Public Result Types

Public result objects are safe to keep after the callback returns.

- `open_result`: returns a `file_descriptor` on success.
- `status_result`: used by operations that only report success or failure.
- `byte_count_result`: used by `write` and `sendfile`.
- `read_result`: owns the read buffer and exposes `bytes()`.
- `stat_result`: contains a portable `file_status` and a copied `uv_stat_t`
  for native interop.
- `path_result`: owns the returned path string.
- `temp_file_result`: owns the generated path and returns the created file descriptor.
- `statfs_result`: contains a copied `uv_statfs_t`.
- `scandir_result`: owns a vector of directory entries.

All result types support `operator bool()`, `status()`, `raw_status()`, and
`error_code()`. `status()` returns `uv::result`; `raw_status()` returns `0` on
success or the raw negative libuv status used for error-code conversion.

`uv::fs` defines its own result types, distinct from those in `uv::fs::raw`, even though they share the same names. The `uv::fs` variants own their data and are safe to copy, store, or pass out of a callback. The `uv::fs::raw` variants may hold views into request-owned memory that expire when `req.cleanup()` is called.

All result types also expose a `.raw()` accessor returning the raw `ssize_t`
from libuv. It is only needed when `ok()`, `status()`, and `error_code()` are
insufficient — for example to distinguish a zero-byte read from a genuine error
when the result value carries semantic meaning beyond success/failure.

Operations such as `rename`, `mkdir`, `rmdir`, `access`, `chmod`, `fchmod`,
`chown`, `fchown`, `lchown`, `utime`, `futime`, `lutime`, `fsync`,
`fdatasync`, `ftruncate`, `link`, and `symlink` return `status_result`.
`fstat` and `lstat` return `stat_result`, like `stat`.

`stat_result::file_status()` is the recommended C++ view of file metadata. It
reports the portable file type, size, permissions, and timestamps as
`std::chrono` time points. `stat_result::native()` remains available when code
needs exact `uv_stat_t` interop.

```cpp
uv::fs::stat(loop, path,
  [](uv::fs::stat_result result) {
    if (!result) {
      return;
    }

    const uv::fs::file_status& status = result.file_status();

    if (status.is_regular()) {
      std::uint64_t bytes = status.size();
      (void)bytes;
    }
  });
```

`mkdtemp` returns a `path_result` with the generated directory path. `mkstemp`
returns `temp_file_result`; callers are responsible for closing the returned
file descriptor.

Example:

```cpp
uv::fs::read(loop, file, 4096, 0,
  [](uv::fs::read_result result) {
    if (!result) {
      return;
    }

    std::span<const std::byte> bytes = result.bytes();
  });
```

`fs::write` copies the submitted bytes into operation-owned storage so the caller does not need to keep the original buffer alive until completion.

```cpp
std::array payload{'o', 'k'};

uv::fs::write(loop, file, std::as_bytes(std::span{payload}), 0,
  [](uv::fs::byte_count_result result) {
    if (result) {
      std::size_t written = result.count();
    }
  });
```

`fs::scandir` copies entries before cleanup.

```cpp
uv::fs::scandir(loop, path, 0,
  [](uv::fs::scandir_result result) {
    if (!result) {
      return;
    }

    for (const uv::fs::directory_entry& entry : result.entries()) {
      std::string_view name = entry.name;
    }
  });
```

## File Descriptors

`file_descriptor` is a thin value wrapper around `uv_file`.

It does not own the file and does not close it in the destructor. `fs::close()` remains explicit because libuv file close is itself an asynchronous filesystem operation.

```cpp
uv::file_descriptor file = result.file();
uv_file native = file.native();
```

## Raw API

`uv::fs::raw` exposes the direct request model.

`raw::request` wraps `uv_fs_t` and can be reused after an operation completes and has been cleaned.

Raw operations do not call `uv_fs_req_cleanup()` automatically. The callback must consume the result and then call `req.cleanup()` before the request is reused or destroyed.

```cpp
uv::fs::raw::request req;

uv::fs::raw::open(loop, req, "file.txt", O_RDONLY, 0,
  [&](uv::fs::raw::request& req, uv::fs::raw::open_result result) {
    auto cleanup = req.scoped_cleanup();

    if (!result) {
      return;
    }

    uv::file_descriptor file = result.file();
  });
```

`scoped_cleanup()` is only a stack guard for `cleanup()`. It does not make raw results independent from the request.

If the same `raw::request` must be reused from inside a callback, clean it before submitting the next operation:

```cpp
uv::fs::raw::open(loop, req, "file.txt", O_RDONLY, 0,
  [&](uv::fs::raw::request& req, uv::fs::raw::open_result result) {
    uv::file_descriptor file;

    {
      auto cleanup = req.scoped_cleanup();
      if (!result) {
        return;
      }

      file = result.file();
    }

    uv::fs::raw::close(loop, req, file,
      [](uv::fs::raw::request& req, uv::fs::raw::status_result) {
        auto cleanup = req.scoped_cleanup();
      });
  });
```

Destroying an active `raw::request` remains a user lifetime error.

## Raw Result Lifetimes

Scalar values such as file descriptors, byte counts, and status codes can be copied freely.

Views into request-owned data are valid only until `req.cleanup()` is called:

- `raw::stat_result::native()`
- `raw::path_result::path()`
- `raw::temp_file_result::path()`
- `raw::statfs_result::native()`
- `raw::scandir_result` entries

Copy request-owned data before cleanup if it must survive the callback.

```cpp
uv::fs::raw::realpath(loop, req, path,
  [](uv::fs::raw::request& req, uv::fs::raw::path_result result) {
    auto cleanup = req.scoped_cleanup();

    if (!result) {
      return;
    }

    std::string path_copy = std::string{result.path()};
  });
```

## Raw Buffers

Raw `read` and `write` do not use stream allocation callbacks. The caller provides storage and must keep it alive until the callback.

```cpp
uv::owned_buffer buffer{4096};

uv::fs::raw::read(loop, req, file, buffer.view(), 0,
  [&](uv::fs::raw::request&, uv::fs::raw::byte_count_result result) {
    auto bytes = buffer.bytes().first(result.count());
  });
```

Low-level FS operations do not copy file contents into wrapper-owned storage.

## Raw Copying and Sending Files

`copyfile` returns `raw::status_result`. Flags use a thin typed enum while still allowing raw integer flags for native interop.

```cpp
uv::fs::raw::copyfile(loop, req, from, to, uv::fs::raw::copyfile_flag::exclusive, callback);
```

`sendfile` returns `raw::byte_count_result`. A successful result may still be partial, so callers should check `count()` against the requested length.

```cpp
uv::fs::raw::sendfile(loop, req, out_file, in_file, offset, length,
  [](uv::fs::raw::request& req, uv::fs::raw::byte_count_result result) {
    auto cleanup = req.scoped_cleanup();
  });
```

## Raw Directory Scanning

Raw `scandir` follows libuv's iterator model and does not copy all entries into a vector.

```cpp
uv::fs::raw::scandir(loop, req, path, 0,
  [](uv::fs::raw::request& req, uv::fs::raw::scandir_result result) {
    auto cleanup = req.scoped_cleanup();

    for (auto entry : result) {
      std::string name = std::string{entry.name()};
    }
  });
```

Raw `scandir_result` is a single-pass range over libuv's iterator. Copy each entry
name before iterator increment or another `next()` call, which releases the previous
entry. Request cleanup also invalidates names. See the
[libuv implementation](https://github.com/libuv/libuv/blob/v1.x/src/uv-common.c).

## Raw Open Directories

`raw::opendir` returns a `raw::directory`, which is a move-only owner for the `uv_dir_t*` allocated by libuv. It must be closed explicitly with `raw::closedir`.

`raw::directory` does not close in its destructor because `uv_fs_closedir()` is asynchronous and requires a loop and a request. In debug builds, destroying a non-empty directory is treated as a programming error.

`raw::readdir` requires a caller-owned `raw::directory_read_buffer` because libuv asks the caller to provide the entry array.

```cpp
uv::fs::raw::directory dir;
uv::fs::raw::directory_read_buffer buffer{32};

uv::fs::raw::opendir(loop, req, path,
  [&](uv::fs::raw::request& req, uv::fs::raw::opendir_result result) {
    {
      auto cleanup = req.scoped_cleanup();
      dir = result.take_directory();
    }

    uv::fs::raw::readdir(loop, req, dir, buffer,
      [&](uv::fs::raw::request& req, uv::fs::raw::readdir_result result) {
        auto cleanup = req.scoped_cleanup();

        for (auto entry : result.entries(buffer)) {
          std::string name = std::string{entry.name()};
        }

        cleanup.cleanup(); // finish readdir before reusing req and closing dir
        uv::fs::raw::closedir(loop, req, std::move(dir),
          [](uv::fs::raw::request& req, uv::fs::raw::status_result) {
            auto cleanup = req.scoped_cleanup();
          });
      });
  });
```

`raw::readdir` borrows `directory&`; `raw::closedir` consumes `directory&&` after successful submission; immediate submission failure leaves the directory owned by the caller. `raw::readdir_result::eof()` reports end-of-directory. Multiple `readdir` calls may be needed when the directory contains more entries than the buffer capacity.

`result.entries(buffer)` ranges over the entries filled in the caller-owned buffer for that `readdir` completion.

`raw::directory_entry::name()` borrows a libuv-allocated name referenced by the
caller-owned entry array. Copy it before request cleanup, which frees the name even
if the array is still alive. Clean the request before the next read or directory
close. See the [libuv readdir contract](https://docs.libuv.org/en/v1.x/fs.html#c.uv_fs_readdir).

## Watchers

Filesystem watchers are handles, not FS requests.

`fs_event` wraps `uv_fs_event_t` and reports backend filesystem notifications.

```cpp
uv::fs_event watcher(loop);

watcher.start(path, [](uv::fs_event& watcher, uv::fs_event_result event) {
  if (!event) {
    return;
  }

  std::string filename = std::string{event.filename()};

  if (event.has(uv::fs_event_kind::rename)) {
  }
});
```

`fs_event_result::filename()` is a borrowed `std::string_view` over the filename pointer passed by libuv. It is valid only during the watcher callback. Copy it if the name must be stored or used asynchronously.

`fs_poll` wraps `uv_fs_poll_t` and reports stat-based changes at an explicit interval.

```cpp
uv::fs_poll watcher(loop);

watcher.start(path, 100ms, [](uv::fs_poll& watcher, uv::fs_poll_result event) {
  if (!event) {
    return;
  }

  uv::fs::file_status previous = event.previous_status();
  uv::fs::file_status current = event.current_status();
});
```

`fs_poll_result::previous()` and `fs_poll_result::current()` are borrowed pointers to the stat snapshots passed by libuv. They are valid only during the watcher callback. Copy the `uv_stat_t` values if they must outlive the callback.
Use `previous_status()` and `current_status()` for portable value snapshots.

Both wrappers follow normal handle lifetime rules: `stop()` stops watching, and `close()` remains asynchronous.

## Callback Forms

`uv::fs` exposes ergonomic runtime callbacks and owns operation state internally.

`uv::fs::raw` supports both runtime callbacks and static callbacks.

```cpp
uv::fs::raw::open(loop, req, "file.txt", O_RDONLY, 0, callback);
uv::fs::raw::open_static<on_open>(loop, req, "file.txt", O_RDONLY, 0);
```

Static callbacks store no runtime callable in `raw::request`.
