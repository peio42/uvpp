#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/net/socket_address.hpp"

namespace uv {

  using process_id = uv_pid_t;

  struct uname_info {
    std::string sysname;
    std::string release;
    std::string version;
    std::string machine;
  };

  struct cpu_times {
    std::uint64_t user = 0;
    std::uint64_t nice = 0;
    std::uint64_t sys = 0;
    std::uint64_t idle = 0;
    std::uint64_t irq = 0;
  };

  struct cpu_info {
    std::string model;
    int speed = 0;
    cpu_times times;
  };

  struct interface_address {
    std::string name;
    std::array<unsigned char, 6> physical_address{};
    bool internal = false;
    socket_address address;
    socket_address netmask;
  };

  namespace detail {

    template<class Lookup>
    std::string os_string(Lookup lookup, std::size_t initial_size = 256) {
      std::string buffer(initial_size, '\0');

      for (;;) {
        auto size = buffer.size();
        auto status = lookup(buffer.data(), &size);

        if (status == UV_ENOBUFS) {
          buffer.assign(size > buffer.size() ? size + 1 : buffer.size() * 2, '\0');
          continue;
        }

        throw_if_error(status);
        buffer.resize(size <= buffer.size() ? size : buffer.size());
        if (!buffer.empty() && buffer.back() == '\0') {
          buffer.pop_back();
        }
        return buffer;
      }
    }

    inline socket_address make_socket_address(const sockaddr_in &address) {
      socket_address out;
      std::memcpy(out.native(), &address, sizeof(address));
      *out.native_len() = static_cast<int>(sizeof(address));
      return out;
    }

    inline socket_address make_socket_address(const sockaddr_in6 &address) {
      socket_address out;
      std::memcpy(out.native(), &address, sizeof(address));
      *out.native_len() = static_cast<int>(sizeof(address));
      return out;
    }

    class cpu_info_guard {
    public:
      cpu_info_guard(uv_cpu_info_t *raw, int count) noexcept : raw_{raw}, count_{count} {}
      cpu_info_guard(const cpu_info_guard&) = delete;
      cpu_info_guard& operator=(const cpu_info_guard&) = delete;

      ~cpu_info_guard() {
        if (raw_) {
          uv_free_cpu_info(raw_, count_);
        }
      }

      uv_cpu_info_t *get() const noexcept { return raw_; }
      int count() const noexcept { return count_; }

    private:
      uv_cpu_info_t *raw_ = nullptr;
      int count_ = 0;
    };

    class interface_address_guard {
    public:
      interface_address_guard(uv_interface_address_t *raw, int count) noexcept : raw_{raw}, count_{count} {}
      interface_address_guard(const interface_address_guard&) = delete;
      interface_address_guard& operator=(const interface_address_guard&) = delete;

      ~interface_address_guard() {
        if (raw_) {
          uv_free_interface_addresses(raw_, count_);
        }
      }

      uv_interface_address_t *get() const noexcept { return raw_; }
      int count() const noexcept { return count_; }

    private:
      uv_interface_address_t *raw_ = nullptr;
      int count_ = 0;
    };

  }

#if UVPP_HAS_OS_ENVIRONMENT
  inline std::optional<std::string> environment_variable(std::string_view name) {
    std::string key{name};
    std::string buffer(256, '\0');

    for (;;) {
      auto size = buffer.size();
      auto status = uv_os_getenv(key.c_str(), buffer.data(), &size);

      if (status == UV_ENOENT) {
        return std::nullopt;
      }

      if (status == UV_ENOBUFS) {
        buffer.assign(size > buffer.size() ? size + 1 : buffer.size() * 2, '\0');
        continue;
      }

      throw_if_error(status);
      buffer.resize(size <= buffer.size() ? size : buffer.size());
      if (!buffer.empty() && buffer.back() == '\0') {
        buffer.pop_back();
      }
      return buffer;
    }
  }

  inline void set_environment_variable(std::string_view name, std::string_view value) {
    std::string key{name};
    std::string storage{value};
    throw_if_error(uv_os_setenv(key.c_str(), storage.c_str()));
  }

  inline void unset_environment_variable(std::string_view name) {
    std::string key{name};
    throw_if_error(uv_os_unsetenv(key.c_str()));
  }
#endif

#if UVPP_HAS_OS_GETPID
  inline process_id pid() noexcept {
    return uv_os_getpid();
  }
#endif

#if UVPP_HAS_OS_GETPPID
  inline process_id parent_pid() noexcept {
    return uv_os_getppid();
  }
#endif

#if UVPP_HAS_OS_PRIORITY
  inline int process_priority(process_id pid) {
    int priority = 0;
    throw_if_error(uv_os_getpriority(pid, &priority));
    return priority;
  }

#if UVPP_HAS_OS_GETPID
  inline int process_priority() {
    return process_priority(pid());
  }
#endif

  inline void set_process_priority(process_id pid, int priority) {
    throw_if_error(uv_os_setpriority(pid, priority));
  }
#endif

#if UVPP_HAS_OS_GETHOSTNAME
  inline std::string hostname() {
    return detail::os_string(uv_os_gethostname);
  }
#endif

#if UVPP_HAS_OS_UNAME
  inline uname_info uname() {
    uv_utsname_t raw{};
    throw_if_error(uv_os_uname(&raw));
    return {raw.sysname, raw.release, raw.version, raw.machine};
  }
#endif

  inline std::vector<cpu_info> cpu_infos() {
    uv_cpu_info_t *raw = nullptr;
    int count = 0;
    throw_if_error(uv_cpu_info(&raw, &count));
    detail::cpu_info_guard guard{raw, count};

    std::vector<cpu_info> out;
    out.reserve(static_cast<std::size_t>(guard.count()));

    for (int i = 0; i < guard.count(); ++i) {
      const auto &item = guard.get()[i];
      out.push_back(cpu_info{
        item.model ? item.model : "",
        item.speed,
        cpu_times{
          item.cpu_times.user,
          item.cpu_times.nice,
          item.cpu_times.sys,
          item.cpu_times.idle,
          item.cpu_times.irq
        }
      });
    }

    return out;
  }

  inline std::vector<interface_address> interface_addresses() {
    uv_interface_address_t *raw = nullptr;
    int count = 0;
    throw_if_error(uv_interface_addresses(&raw, &count));
    detail::interface_address_guard guard{raw, count};

    std::vector<interface_address> out;
    out.reserve(static_cast<std::size_t>(guard.count()));

    for (int i = 0; i < guard.count(); ++i) {
      const auto &item = guard.get()[i];
      interface_address address;
      address.name = item.name ? item.name : "";
      std::memcpy(address.physical_address.data(), item.phys_addr, address.physical_address.size());
      address.internal = item.is_internal != 0;

      if (item.address.address4.sin_family == AF_INET) {
        address.address = detail::make_socket_address(item.address.address4);
        address.netmask = detail::make_socket_address(item.netmask.netmask4);
      } else if (item.address.address4.sin_family == AF_INET6) {
        address.address = detail::make_socket_address(item.address.address6);
        address.netmask = detail::make_socket_address(item.netmask.netmask6);
      }

      out.push_back(std::move(address));
    }

    return out;
  }

  inline std::array<double, 3> load_average() noexcept {
    std::array<double, 3> out{};
    uv_loadavg(out.data());
    return out;
  }

  inline std::chrono::duration<double> uptime() {
    double seconds = 0.0;
    throw_if_error(uv_uptime(&seconds));
    return std::chrono::duration<double>{seconds};
  }

  inline std::uint64_t total_memory() noexcept {
    return uv_get_total_memory();
  }

  inline std::uint64_t free_memory() noexcept {
    return uv_get_free_memory();
  }

#if UVPP_HAS_AVAILABLE_PARALLELISM
  inline unsigned int available_parallelism() noexcept {
    return uv_available_parallelism();
  }
#endif

  inline std::uint64_t resident_set_memory() {
    std::size_t rss = 0;
    throw_if_error(uv_resident_set_memory(&rss));
    return static_cast<std::uint64_t>(rss);
  }

#if UVPP_HAS_OS_TMPDIR
  inline std::string tmpdir() {
    return detail::os_string(uv_os_tmpdir);
  }
#endif

#if UVPP_HAS_OS_HOMEDIR
  inline std::string homedir() {
    return detail::os_string(uv_os_homedir);
  }
#endif

  inline std::string cwd() {
    return detail::os_string(uv_cwd);
  }

  inline void chdir(std::string_view path) {
    std::string storage{path};
    throw_if_error(uv_chdir(storage.c_str()));
  }

  inline std::string exepath() {
    return detail::os_string(uv_exepath);
  }

#if UVPP_HAS_SLEEP
  template<class Rep, class Period>
  void sleep_blocking_for(std::chrono::duration<Rep, Period> duration) {
    using namespace std::chrono;

    if (duration <= duration.zero()) {
      uv_sleep(0);
      return;
    }

    const auto millis = ceil<milliseconds>(duration);
    const auto max_millis = static_cast<milliseconds::rep>(std::numeric_limits<unsigned int>::max());
    if (millis.count() > max_millis) {
      throw std::length_error{"uv::sleep_blocking_for duration exceeds libuv limits"};
    }

    uv_sleep(static_cast<unsigned int>(millis.count()));
  }
#endif

}
