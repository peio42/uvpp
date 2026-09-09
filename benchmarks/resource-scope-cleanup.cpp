#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <uv.h>

#include "uvpp/co/resource_scope.hpp"
#include "uvpp/handles/tcp.hpp"
#include "uvpp/net/tcp_connection.hpp"

namespace {

// This counts C++ allocations made while resource_scope::finish() is active.
// It intentionally excludes libuv's C allocations and setup allocations.
std::atomic<std::size_t> cpp_allocations{0};
thread_local bool count_cpp_allocations = false;

void *allocate(std::size_t size) {
  if (count_cpp_allocations) {
    cpp_allocations.fetch_add(1, std::memory_order_relaxed);
  }
  if (void *ptr = std::malloc(size == 0 ? 1 : size)) {
    return ptr;
  }
  throw std::bad_alloc{};
}

void *allocate_aligned(std::size_t size, std::size_t alignment) {
  if (count_cpp_allocations) {
    cpp_allocations.fetch_add(1, std::memory_order_relaxed);
  }
  void *ptr = nullptr;
  if (posix_memalign(&ptr, alignment, size == 0 ? alignment : size) == 0) {
    return ptr;
  }
  throw std::bad_alloc{};
}

class allocation_window {
public:
  allocation_window() : before_{cpp_allocations.load(std::memory_order_relaxed)} {
    count_cpp_allocations = true;
  }

  ~allocation_window() { count_cpp_allocations = false; }

  std::size_t count() const {
    return cpp_allocations.load(std::memory_order_relaxed) - before_;
  }

private:
  std::size_t before_ = 0;
};

struct cleanup_sample {
  std::chrono::nanoseconds elapsed{};
  std::size_t allocations = 0;
};

cleanup_sample run_once(std::size_t connection_count) {
  uv::loop loop;
  uv::tcp server(loop);
  std::vector<std::unique_ptr<uv::tcp>> peers;
  int server_status = 0;
  server.bind(uv::ipv4{"127.0.0.1", 0});
  const auto address = uv::ipv4{"127.0.0.1", server.sockname().port()};
  server.listen([&](uv::tcp &listener, uv::result status) {
    if (!status) {
      server_status = status.status();
      return;
    }
    auto peer = std::make_unique<uv::tcp>(loop);
    try {
      listener.accept(*peer);
    } catch (const uv::error &error) {
      server_status = error.code().value();
      peer->close();
      return;
    }
    peers.push_back(std::move(peer));
  });

  cleanup_sample sample;
  auto cleanup = [&]() -> uv::co::task<void> {
    uv::co::resource_scope resources(loop);
    for (std::size_t index = 0; index != connection_count; ++index) {
      (void)resources.own(co_await uv::tcp_connection::connect(address));
    }

    {
      allocation_window allocations;
      const auto started = std::chrono::steady_clock::now();
      co_await resources.finish();
      sample.elapsed = std::chrono::steady_clock::now() - started;
      sample.allocations = allocations.count();
    }

    for (auto &peer : peers) {
      if (!peer->closing()) {
        peer->close();
      }
    }
    server.close();
  };

  auto execution = uv::co::spawn(loop, cleanup());
  loop.run();
  execution.rethrow_if_failed();
  if (server_status < 0) {
    throw uv::error{server_status};
  }
  if (peers.size() != connection_count) {
    throw std::runtime_error{"cleanup benchmark did not accept every connection"};
  }
  loop.close();
  return sample;
}

std::size_t parse_positive(char *value, const char *name) {
  char *end = nullptr;
  const auto parsed = std::strtoul(value, &end, 10);
  if (*value == '\0' || *end != '\0' || parsed == 0) {
    throw std::invalid_argument{std::string{"invalid "} + name};
  }
  return parsed;
}

long long microseconds(std::chrono::nanoseconds duration) {
  return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

} // namespace

void *operator new(std::size_t size) { return allocate(size); }
void *operator new[](std::size_t size) { return allocate(size); }
void *operator new(std::size_t size, std::align_val_t alignment) {
  return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
  return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
  try {
    return allocate(size);
  } catch (...) {
    return nullptr;
  }
}
void *operator new[](std::size_t size, const std::nothrow_t &tag) noexcept {
  return ::operator new(size, tag);
}
void *operator new(std::size_t size, std::align_val_t alignment,
    const std::nothrow_t &) noexcept {
  try {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
  } catch (...) {
    return nullptr;
  }
}
void *operator new[](std::size_t size, std::align_val_t alignment,
    const std::nothrow_t &tag) noexcept {
  return ::operator new(size, alignment, tag);
}
void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete[](void *ptr) noexcept { std::free(ptr); }
void operator delete(void *ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void *ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete(void *ptr, std::align_val_t) noexcept { std::free(ptr); }
void operator delete[](void *ptr, std::align_val_t) noexcept { std::free(ptr); }
void operator delete(void *ptr, std::size_t, std::align_val_t) noexcept { std::free(ptr); }
void operator delete[](void *ptr, std::size_t, std::align_val_t) noexcept { std::free(ptr); }
void operator delete(void *ptr, const std::nothrow_t &) noexcept { std::free(ptr); }
void operator delete[](void *ptr, const std::nothrow_t &) noexcept { std::free(ptr); }
void operator delete(void *ptr, std::align_val_t, const std::nothrow_t &) noexcept {
  std::free(ptr);
}
void operator delete[](void *ptr, std::align_val_t, const std::nothrow_t &) noexcept {
  std::free(ptr);
}

int main(int argc, char **argv) {
  try {
    const auto rounds = argc > 1 ? parse_positive(argv[1], "round count") : 20U;
    const auto connections = argc > 2 ? parse_positive(argv[2], "connection count") : 32U;
    std::vector<cleanup_sample> samples;
    samples.reserve(rounds);
    for (std::size_t round = 0; round != rounds; ++round) {
      samples.push_back(run_once(connections));
    }

    std::vector<long long> latencies;
    latencies.reserve(samples.size());
    std::size_t allocation_total = 0;
    for (const auto sample : samples) {
      latencies.push_back(microseconds(sample.elapsed));
      allocation_total += sample.allocations;
    }
    std::sort(latencies.begin(), latencies.end());
    const auto latency_total = std::accumulate(latencies.begin(), latencies.end(), 0LL);
    const auto p95_index = (latencies.size() * 95 + 99) / 100 - 1;

    std::cout << "resource_scope cleanup metrics\n"
              << "compiler: " << __VERSION__ << "\n"
              << "libuv: " << uv_version_string() << "\n"
              << "rounds: " << rounds << ", connections per round: " << connections << "\n"
              << "C++ allocations during finish (excludes libuv C allocations): mean "
              << std::fixed << std::setprecision(2)
              << static_cast<double>(allocation_total) / static_cast<double>(samples.size()) << "\n"
              << "finish latency (us): min " << latencies.front()
              << ", mean " << static_cast<double>(latency_total) / static_cast<double>(latencies.size())
              << ", p95 " << latencies[p95_index]
              << ", max " << latencies.back() << "\n";
  } catch (const uv::error &error) {
    if (error.code().value() == UV_EPERM) {
      std::cout << "resource_scope cleanup metrics skipped: loopback TCP is not permitted\n";
      return 0;
    }
    std::cerr << "resource_scope cleanup metrics failed: " << error.what() << "\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "resource_scope cleanup metrics failed: " << error.what() << "\n";
    return 1;
  }
}
