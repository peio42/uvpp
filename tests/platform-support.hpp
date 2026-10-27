#pragma once

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <fcntl.h>
#include <uv.h>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace uvpp::test {
// Resolve symlinks and Windows 8.3 aliases before starting a watcher. libuv's
// Windows backend compares the watched path with GetLongPathNameW results.
inline std::filesystem::path filesystem_test_directory() {
  auto directory = std::filesystem::canonical(std::filesystem::temp_directory_path());
#ifdef _WIN32
  const auto required = GetLongPathNameW(directory.c_str(), nullptr, 0);
  if (required == 0) throw std::runtime_error{"GetLongPathNameW failed"};
  std::wstring expanded(required, L'\0');
  const auto length = GetLongPathNameW(directory.c_str(), expanded.data(), required);
  if (length == 0 || length >= required) throw std::runtime_error{"GetLongPathNameW failed"};
  expanded.resize(length);
  directory = std::filesystem::path{expanded};
#endif
  return directory;
}

inline std::string executable_path() {
  std::string path(32768, '\0');
  std::size_t size = path.size();
  if (uv_exepath(path.data(), &size) < 0) throw std::runtime_error{"uv_exepath failed"};
  path.resize(size);
  return path;
}

inline std::string pipe_path(std::string_view suffix) {
#ifdef _WIN32
  return R"(\\.\pipe\uvpp-)" + std::to_string(uv_os_getpid()) + "-" + std::string{suffix};
#else
  // Keep Unix socket paths short, including on macOS where sun_path is 104 bytes.
  return "/tmp/uvpp-" + std::to_string(uv_os_getpid()) + "-" + std::string{suffix};
#endif
}

inline uv_uid_t test_uid() {
#ifdef _WIN32
  return 0;
#else
  return ::getuid();
#endif
}
inline uv_gid_t test_gid() {
#ifdef _WIN32
  return 0;
#else
  return ::getgid();
#endif
}

inline int open_file(const std::filesystem::path &path, int flags, int mode = 0644) {
#ifdef _WIN32
  int fd = -1;
  const auto error = ::_wsopen_s(&fd, path.c_str(), flags | _O_BINARY, _SH_DENYNO, mode & 0600);
  return error == 0 ? fd : -1;
#else
  return ::open(path.c_str(), flags, mode);
#endif
}
inline int close_file(int fd) {
#ifdef _WIN32
  return ::_close(fd);
#else
  return ::close(fd);
#endif
}

// Run before Google Test initialization. The test binary doubles as a portable
// child fixture, avoiding shell syntax, quoting and external utility dependencies.
inline int child_main(int argc, char **argv) {
  if (argc < 2 || std::string_view{argv[1]} != "--uvpp-child") return -1;
  if (argc < 3) return 100;
  const std::string_view mode{argv[2]};
  if (mode == "exit") {
    if (argc > 4) std::this_thread::sleep_for(std::chrono::milliseconds{std::stoi(argv[4])});
    return argc >= 4 ? std::stoi(argv[3]) : 100;
  }
  if (mode == "environment" && argc == 4) {
    char value[128];
    std::size_t size = sizeof(value);
    const bool present = uv_os_getenv("UVPP_TEST_INHERIT_VAR", value, &size) == 0;
    return present == (std::string_view{argv[3]} == "present") ? 0 : 101;
  }
  if (mode == "cwd") {
    return argc == 5 && std::string_view{argv[3]} == "value" &&
        std::filesystem::equivalent(std::filesystem::current_path(), argv[4]) ? 0 : 102;
  }
  return 103;
}
} // namespace uvpp::test
