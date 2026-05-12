# Process

`process` is a low-level wrapper over `uv_process_t`. The first v2 API keeps process spawning explicit and close to libuv where libuv exposes platform-specific behavior.

## Options

`process_options` owns the strings and arrays needed to build `uv_process_options_t` for the duration of `uv_spawn`.

```cpp
process_options options;
options.file = "/bin/sh";
options.arguments = {"-c", "exit 7"};
```

Rules:

- `file` is the executable passed to libuv and is also used as `argv[0]`;
- `arguments` contains only arguments after `argv[0]`;
- `environment` contains raw `KEY=VALUE` entries and an empty vector inherits the parent environment;
- `cwd` is optional and an empty string inherits the parent working directory;
- `flags` is a raw `uv_process_flags` bitmask because the flag set is already compact and libuv-specific;
- `stdio` is currently a `std::vector<uv_stdio_container_t>` as an explicit low-level escape hatch.

`process_options` only needs to outlive the `process` constructor call. `uv_spawn` consumes the native options synchronously. After construction, uvpp stores only the process handle and the exit callback slot.

## Stdio Policy

The initial API deliberately does not hide `uv_stdio_container_t`.

Process stdio is one of the places where libuv's model is detailed and platform-sensitive: ignored streams, inherited file descriptors, inherited streams, readable/writable pipes, and detached behavior interact with `flags`. A premature C++ wrapper would likely be incomplete or misleading.

The intended later layer can add typed helpers such as:

```cpp
process_stdio::ignore();
process_stdio::inherit_fd(1);
process_stdio::inherit_stream(stream);
process_stdio::create_pipe(pipe, process_stdio::readable);
```

Until that layer exists, the low-level API keeps `stdio` native and explicit. This preserves minimal overhead and avoids inventing ownership semantics before the process/pipe integration is fully designed.

## Callback Forms

The process exit callback is fixed when `uv_spawn` is called, so `process` cannot use a later `start_static<Callback>()` shape. It supports two construction paths instead:

```cpp
uvpp::process child(loop, options, [](uvpp::process& child, uvpp::process_exit exit) {
  child.close();
});

uvpp::process static_child(loop, options, uvpp::process::static_callback<on_exit>{});
```

The runtime form stores one exit callback in the process object. The static form stores no callable.
