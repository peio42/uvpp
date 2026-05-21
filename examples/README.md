# Examples

Build all examples with:

```sh
make examples
```

The binaries are written under `build/<compiler>/examples/`.

- `tcp-echo-server.cpp`: TCP echo server on `0.0.0.0:2345`.
- `udp-ping-pong.cpp`: UDP ping/pong exchange between two handles on the same loop.
- `timer-watchers.cpp`: repeating timer coordinated with `prepare` and `check` watchers.
- `process-stdout.cpp`: child process execution with stdout captured through `uv::pipe`.
