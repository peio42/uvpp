# Future Features

This page tracks features identified for future uvpp v2 releases. These
additions are intended to extend the library without changing the published
low-level v2 contracts.

## Coroutines

- Coroutine adapters for selected asynchronous operations.
- Coroutine support should layer on top of the explicit request/lifetime model
  rather than replacing the low-level API.

## Thread and Synchronization Primitives

libuv provides cross-platform wrappers around OS threading and synchronization
primitives. In a C++20 codebase `std::thread`, `std::mutex`, and related
standard facilities cover most use cases, so these wrappers have lower
priority. A future v2 release may expose them for callers that must stay
within the libuv ecosystem.

Planned scope: `uv_thread_t`, `uv_mutex_t`, `uv_rwlock_t`, `uv_sem_t`,
`uv_cond_t`, `uv_barrier_t`, `uv_key_t`, and `uv_once`.

## System and Process Utilities

libuv exposes a collection of cross-platform system information functions.
These are lower priority than the async I/O surface but may be added in a
future v2 release.

Planned scope:

- **Environment**: `uv_os_getenv`, `uv_os_setenv`, `uv_os_unsetenv`
- **Process**: `uv_os_getpid`, `uv_os_getppid`, `uv_os_getpriority`,
  `uv_os_setpriority`
- **System information**: `uv_os_gethostname`, `uv_os_uname`, `uv_cpu_info`,
  `uv_interface_addresses`, `uv_loadavg`, `uv_uptime`, `uv_get_total_memory`,
  `uv_get_free_memory`, `uv_available_parallelism`, `uv_resident_set_memory`
- **Directories and paths**: `uv_os_tmpdir`, `uv_os_homedir`, `uv_cwd`,
  `uv_chdir`, `uv_exepath`
- **Misc**: `uv_sleep`
