# uvpp Documentation

uvpp is a header-only C++20 wrapper around libuv. Include `uvpp/uv.hpp`, use namespace `uv`, and link with libuv and pthread.

## User Guides

- [Tutorial](tutorial/): a progressive introduction to the event loop, handles,
  callbacks, ownership, buffers, streams, and filesystem operations.
- [Getting started](getting-started.md): requirements, build flags, and a minimal timer program.
- [Loop](loop.md): running the event loop, backend information, metrics, and handle walking.
- [Callbacks](callbacks.md): runtime callbacks, static callbacks, result objects, and exception boundaries.
- [Errors](errors.md): immediate failures, asynchronous completion failures, and `uv::error`.
- [Ownership and lifetime](ownership-and-lifetime.md): handles, requests, close callbacks, and borrowed callback data.
- [Buffers](buffers.md): `buffer_view`, `owned_buffer`, spans, and async buffer lifetime.
- [Streams](streams.md): TCP/pipe/TTY stream operations, accept, read, write, and shutdown requests.
- [UDP](udp.md): datagram sockets, immediate sends, receive callbacks, and multicast helpers.
- [Network utilities](network.md): DNS lookup requests and interface-index helpers.
- [Filesystem](filesystem.md): recommended `uv::fs` APIs and the lower-level `uv::fs::raw` layer.
- [Process](process.md): process spawning and process options.
- [Thread pool work](threadpool.md): `queue_work` requests and worker-thread completion.
- [Random bytes](random.md): synchronous and asynchronous random byte generation.
- [Future features](future.md): post-`2.1.0` features identified for `2.x`.

## Design Notes

The files under [design](design/) are for contributors and maintainers. They describe API strategy and implementation constraints, not the primary user-facing documentation. Start with the [design index](design/index.md).
