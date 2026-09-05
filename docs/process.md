# Process

`process` is a low-level wrapper over `uv_process_t`. The first v2 API keeps process spawning explicit and close to libuv where libuv exposes platform-specific behavior.

## Options

`process_options` owns the strings and arrays needed to build `uv_process_options_t` for the duration of `uv_spawn`.

```cpp
auto options = uv::process_options::make("/bin/sh")
  .args({"-c", "exit 7"});
```

The fluent helpers mutate and return the `process_options` value, so direct field initialization is still available for low-level code:

```cpp
uv::process_options options;
options.file = "/bin/sh";
options.arguments = {"-c", "exit 7"};
```

Rules:

- `file` is the executable passed to libuv and is also used as `argv[0]`;
- `arguments` contains only arguments after `argv[0]`;
- `environment` contains raw `KEY=VALUE` entries;
- `inherit_parent_environment` controls whether an empty environment inherits the parent environment;
- `working_directory` is optional and an empty string inherits the parent working directory;
- `flags` is a raw `uv_process_flags` bitmask because the flag set is already compact and libuv-specific;
- `uid` and `gid` are copied to libuv when `set_uid()` / `set_gid()` enable the corresponding flags;
- `stdio_entries` is a `std::vector<uv_stdio_container_t>` as an explicit low-level escape hatch.

Common construction helpers are available for ordinary process launches:

```cpp
auto options = uv::process_options::make("git")
  .arg("status")
  .args({"--short", "--branch"})
  .cwd(repo)
  .inherit_stdout()
  .inherit_stderr();
```

Flag helpers keep common libuv flags readable:

```cpp
auto options = uv::process_options::make("server")
  .detached()
  .set_uid(uid)
  .set_gid(gid)
  .windows_hide();
```

`set_uid()` and `set_gid()` are Unix-oriented libuv options. On Windows, libuv
reports `UV_ENOTSUP` if those flags are used. Newer Windows-only flags are
available only when the installed libuv headers expose them, for example
`windows_file_path_exact_name()` behind
`UVPP_HAS_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME`.

`process_options` only needs to outlive the `process` constructor call. `uv_spawn` consumes the native options synchronously. After construction, the wrapper stores only the process handle and the exit callback slot.

If `uv_spawn` fails after libuv has attached its native process handle to the
loop, the constructor closes that handle and retains its native storage until
libuv has delivered the close callback. It then throws `uv::error` as usual.
The failed construction therefore leaves no dangling handle in the loop; run the
loop before closing it to let this internal close complete.

## Stdio Policy

The API deliberately does not hide `uv_stdio_container_t`.

Process stdio is one of the places where libuv's model is detailed and platform-sensitive: ignored streams, inherited file descriptors, inherited streams, readable/writable pipes, and detached behavior interact with `flags`. A premature C++ wrapper would likely be incomplete or misleading.

The low-level helper type can produce native stdio entries:

```cpp
uv::process_stdio::ignore();
uv::process_stdio::inherit_fd(1);
uv::process_stdio::inherit_stream(stream);
uv::process_stdio::readable_pipe(pipe);
```

You can pass those entries directly:

```cpp
auto options = uv::process_options::make("tool")
  .stdio({
    uv::process_stdio::ignore(),
    uv::process_stdio::inherit_fd(1),
    uv::process_stdio::inherit_fd(2),
  });
```

For the standard descriptors, semantic shortcuts fill the corresponding `stdio_entries` slots:

```cpp
auto options = uv::process_options::make("tool")
  .ignore_stdin()
  .inherit_stdout()
  .inherit_stderr();
```

## Callback Forms

The process exit callback is fixed when `uv_spawn` is called, so `process` cannot use a later `start_static<Callback>()` shape. It supports two construction paths instead:

```cpp
uv::process child(loop, options, [](uv::process& child, uv::process_exit exit) {
  child.close();
});

uv::process static_child(loop, options, uv::process::static_callback<on_exit>{});
```

The runtime form stores one exit callback in the process object. The static form stores no callable.

## Process Control

After a successful spawn, `process::pid()` returns the child PID using
`uv_process_get_pid()` when the installed libuv provides it.

```cpp
auto pid = child.pid();
```

Use `child.kill(signum)` for a process handle. This avoids targeting an
unrelated process if a cached PID is later reused by the operating system.
`uv::process::kill(pid, signum)` remains available for the raw PID case.

`uv::process::disable_stdio_inheritance()` exposes libuv's process-wide helper
for preventing accidental inheritance of parent file descriptors by future
children. It should be called early in program startup when used.
