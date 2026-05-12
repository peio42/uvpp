# Filesystem

The filesystem API is split into two layers:

- `uv::fs`: the recommended C++ API. It owns the internal `uv_fs_t`, performs `uv_fs_req_cleanup()` automatically, and returns values that can safely outlive the callback.
- `uv::fs::raw`: the direct libuv-shaped API. It exposes `uv_fs_t` through `raw::request`, requires explicit cleanup, and keeps request-owned views request-scoped.

This split keeps the public API aligned with the rest of v2: lifetime rules are encoded by default, while the low-level layer remains available when the caller wants exact libuv control or zero-extra-copy behavior.

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

The initial public API includes:

- `fs::open`
- `fs::close`
- `fs::read`
- `fs::write`
- `fs::stat`
- `fs::unlink`
- `fs::rename`
- `fs::mkdir`
- `fs::rmdir`
- `fs::fstat`
- `fs::lstat`
- `fs::access`
- `fs::chmod`
- `fs::chown`
- `fs::utime`
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
- `stat_result`: contains a copied `uv_stat_t`.
- `path_result`: owns the returned path string.
- `scandir_result`: owns a vector of directory entries.

All result types support `operator bool()` and `error_code()`.

Operations such as `rename`, `mkdir`, `rmdir`, `access`, `chmod`, `chown`, `utime`, `fsync`, `fdatasync`, `ftruncate`, `link`, and `symlink` return `status_result`. `fstat` and `lstat` return `stat_result`, like `stat`.

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

    uv::fs::raw::directory_entry entry;
    while (result.next(entry)) {
      std::string name = std::string{entry.name()};
    }
  });
```

The entry name is request-owned. Copy it before cleanup if it must outlive the callback.

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

        for (std::size_t i = 0; i < result.count(); ++i) {
          auto entry = buffer.entry(i);
        }

        uv::fs::raw::closedir(loop, req, std::move(dir),
          [](uv::fs::raw::request& req, uv::fs::raw::status_result) {
            auto cleanup = req.scoped_cleanup();
          });
      });
  });
```

`raw::readdir` borrows `directory&`; `raw::closedir` consumes `directory&&` and invalidates it immediately. `raw::readdir_result::eof()` reports end-of-directory. Multiple `readdir` calls may be needed when the directory contains more entries than the buffer capacity.

`raw::directory_entry::name()` returns a `std::string_view` pointing into the `directory_read_buffer`. It is valid only while that buffer is alive. Copy it before the buffer is destroyed or reused for the next `readdir` call.

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

  const uv_stat_t* previous = event.previous();
  const uv_stat_t* current = event.current();
});
```

`fs_poll_result::previous()` and `fs_poll_result::current()` are borrowed pointers to the stat snapshots passed by libuv. They are valid only during the watcher callback. Copy the `uv_stat_t` values if they must outlive the callback.

Both wrappers follow normal handle lifetime rules: `stop()` stops watching, and `close()` remains asynchronous.

## Callback Forms

`uv::fs` currently exposes ergonomic runtime callbacks and owns operation state internally.

`uv::fs::raw` supports both runtime callbacks and static callbacks.

```cpp
uv::fs::raw::open(loop, req, "file.txt", O_RDONLY, 0, callback);
uv::fs::raw::open_static<on_open>(loop, req, "file.txt", O_RDONLY, 0);
```

Static callbacks store no runtime callable in `raw::request`.
