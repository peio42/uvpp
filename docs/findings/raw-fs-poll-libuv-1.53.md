# Raw filesystem polling prevents builds with libuv 1.53

- **Status:** confirmed by public-header/source inspection; compile reproduction not run
- **Severity:** P1
- **Component:** `uv::raw::fs_poll` and `<uvpp/uv.hpp>`
- **Affected versions:** libuv 1.53.0 and later versions that retain the removal
- **Upstream:** libuv removed `uv_fs_poll_t` in [issue #4543](https://github.com/libuv/libuv/issues/4543), implemented in [v1.53.0](https://github.com/libuv/libuv/blob/v1.53.0/include/uv.h).

## Observation

`include/uvpp/uv.hpp` unconditionally includes `handles/fs_poll.hpp`. That
wrapper embeds `uv_fs_poll_t` and calls `uv_fs_poll_init/start/stop/getpath`.
There is no `UVPP_HAS_FS_POLL` capability gate in `core/version.hpp`.
The libuv 1.53.0 public header retains an opaque typedef for `uv_fs_poll_t`, but
does not provide its public struct definition or the filesystem-poll function
declarations. A translation unit including the uvpp umbrella therefore cannot
compile against that version, including when the application does not use
filesystem polling.

The repository CI references libuv 1.53.0, but its Windows poll rollback job
does not establish that the general uvpp headers compile against that header
set. No minimum supported libuv version is declared in the reviewed docs.

## Reproduction

Configure a C++20 translation unit to use the libuv 1.53.0 headers and compile:

```cpp
#include <uvpp/uv.hpp>

int main() {}
```

Compilation is expected to fail in `handles/fs_poll.hpp` on the incomplete
native type and removed declarations. This compile was not run during the
source audit.

## Recommended resolution

Declare the supported libuv range and version-gate or remove the `raw::fs_poll`
type and its umbrella include for libuv 1.53+. Keep the older surface available
only where its native API exists. Update user documentation and compile-only CI
coverage to test the umbrella header at both ends of the declared range. Do not
recreate filesystem polling under the removed native type as a compatibility
workaround.
