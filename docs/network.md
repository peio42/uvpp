# Network Utilities

Network utility APIs cover asynchronous DNS lookups and small interface-index
helpers that do not belong to a specific socket handle.

## DNS

`getaddrinfo()` and `getnameinfo()` are one-shot asynchronous operations. The
request object owns callback state and copied submission inputs until
completion.

```cpp
uv::loop loop;
uv::getaddrinfo_request request;

addrinfo hints{};
hints.ai_family = AF_INET;
hints.ai_socktype = SOCK_STREAM;

uv::getaddrinfo(loop, request, "localhost", "80", &hints,
  [](uv::getaddrinfo_request&, uv::getaddrinfo_result result) {
    if (!result) {
      return;
    }

    for (uv::addrinfo_view entry : result) {
      auto* address = entry.address();
      auto family = entry.family();
      (void)address;
      (void)family;
    }
  });

loop.run();
loop.close();
```

`getaddrinfo_result` owns the native `addrinfo` list and frees it with
`uv_freeaddrinfo()` when the result is destroyed. Copy any address information
that must outlive the callback result object.

Either node or service may be omitted with `nullptr`, matching libuv's native
contract.

```cpp
uv::getaddrinfo(loop, request, nullptr, "443",
  [](uv::getaddrinfo_request&, uv::getaddrinfo_result result) {
    (void)result;
  });
```

Reverse lookup uses `getnameinfo_request` and returns copied strings.

```cpp
uv::getnameinfo_request reverse;
uv::ipv4 address{"127.0.0.1", 443};

uv::getnameinfo(loop, reverse, address, NI_NUMERICHOST | NI_NUMERICSERV,
  [](uv::getnameinfo_request&, uv::getnameinfo_result result) {
    if (!result) {
      return;
    }

    auto host = result.hostname();
    auto service = result.service();
    (void)host;
    (void)service;
  });
```

Immediate submission failures throw `uv::error`. Completion failures are
reported through the result object.

Static callback overloads are available as `getaddrinfo_static<Callback>()` and
`getnameinfo_static<Callback>()`.

## Interface Index Helpers

`interface_name()` maps a network interface index to its platform interface
name. `interface_identifier()` returns an identifier suitable for scoped IPv6
addresses; on Unix this is usually the interface name, while on Windows it may
be a numeric identifier string.

These helpers are available when building against libuv 1.16.0 or newer.

```cpp
#if UVPP_HAS_IF_INDEX_TO_NAME
std::string name = uv::interface_name(1);
#endif

#if UVPP_HAS_IF_INDEX_TO_IID
std::string identifier = uv::interface_identifier(1);
#endif
```

Both functions throw `uv::error` if libuv cannot resolve the index.
