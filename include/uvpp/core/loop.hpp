#pragma once

#include <memory>
#include <system_error>
#include <type_traits>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
// handle_view.hpp has no dependency on loop.hpp, so this include is safe here
// and lets handle_view be a complete type throughout this header.
#include "uvpp/handles/handle_view.hpp"

namespace uv {

struct loop_metrics {
  uint64_t loop_count;
  uint64_t events;
  uint64_t events_waiting;
};

class loop_view {
public:
  explicit loop_view(uv_loop_t *raw) noexcept : raw_{raw} {}

  uv_loop_t *native() const noexcept { return raw_; }

  bool run(uv_run_mode mode = UV_RUN_DEFAULT) {
    return throw_if_error(uv_run(raw_, mode)) != 0;
  }

  void stop() noexcept { uv_stop(raw_); }

  bool alive() const noexcept { return uv_loop_alive(raw_) != 0; }

  void update_time() noexcept { uv_update_time(raw_); }

  uint64_t now() const noexcept { return uv_now(raw_); }

  // Returns the I/O backend file descriptor (not available on Windows).
  int backend_fd() const noexcept { return uv_backend_fd(raw_); }

  // Returns the timeout the backend should use on the next poll, in
  // milliseconds. -1 means infinite, 0 means no wait.
  int backend_timeout() const noexcept { return uv_backend_timeout(raw_); }

  // Returns accumulated idle time in nanoseconds. Requires
  // enable_metrics_idle_time() to have been called first.
  uint64_t metrics_idle_time() const noexcept {
    return uv_metrics_idle_time(raw_);
  }

  loop_metrics metrics_info() const {
    uv_metrics_t m{};
    throw_if_error(uv_metrics_info(raw_, &m));
    return {m.loop_count, m.events, m.events_waiting};
  }

  // Block delivery of signum in the event loop thread.
  void configure_block_signal(int signum) {
    throw_if_error(uv_loop_configure(raw_, UV_LOOP_BLOCK_SIGNAL, signum));
  }

  // Enable collection of idle time metrics (prerequisite for metrics_idle_time).
  void enable_metrics_idle_time() {
    throw_if_error(uv_loop_configure(raw_, UV_METRICS_IDLE_TIME));
  }

  // Reinitialize this loop after a fork() call (call from the child only).
  void fork() { throw_if_error(uv_loop_fork(raw_)); }

  // Walk all handles attached to this loop. F must be callable as void(handle_view).
  // Handles not created by uvpp can be safely observed; use handle_view::as<T>()
  // only for handles you know were created by uvpp.
  template<class F>
  void walk(F &&callback);

  // Collect all handles into a vector. Suitable for range-based for and
  // std::ranges algorithms. The vector holds lightweight handle_view values
  // (one pointer each). For large handle counts, prefer walk() directly.
  std::vector<handle_view> handles();

private:
  uv_loop_t *raw_;
};

class loop {
public:
  loop() { throw_if_error(uv_loop_init(&raw_)); }

  loop(const loop &) = delete;
  loop &operator=(const loop &) = delete;
  loop(loop &&) = delete;
  loop &operator=(loop &&) = delete;

  ~loop() = default;

  uv_loop_t *native() noexcept { return &raw_; }
  const uv_loop_t *native() const noexcept { return &raw_; }

  loop_view view() noexcept { return loop_view{&raw_}; }

  bool run(uv_run_mode mode = UV_RUN_DEFAULT) { return view().run(mode); }

  void stop() noexcept { uv_stop(&raw_); }

  void close() { throw_if_error(uv_loop_close(&raw_)); }

  std::error_code try_close() noexcept {
    return make_error_code(uv_loop_close(&raw_));
  }

  bool alive() const noexcept {
    return uv_loop_alive(const_cast<uv_loop_t *>(&raw_)) != 0;
  }

  void update_time() noexcept { uv_update_time(&raw_); }

  uint64_t now() const noexcept {
    return uv_now(const_cast<uv_loop_t *>(&raw_));
  }

  int backend_fd() const noexcept { return uv_backend_fd(&raw_); }

  int backend_timeout() const noexcept { return uv_backend_timeout(&raw_); }

  uint64_t metrics_idle_time() const noexcept {
    return uv_metrics_idle_time(const_cast<uv_loop_t *>(&raw_));
  }

  loop_metrics metrics_info() const {
    uv_metrics_t m{};
    throw_if_error(uv_metrics_info(const_cast<uv_loop_t *>(&raw_), &m));
    return {m.loop_count, m.events, m.events_waiting};
  }

  void configure_block_signal(int signum) {
    view().configure_block_signal(signum);
  }

  void enable_metrics_idle_time() { view().enable_metrics_idle_time(); }

  void fork() { view().fork(); }

  template<class F>
  void walk(F &&callback) { view().walk(std::forward<F>(callback)); }

  std::vector<handle_view> handles() { return view().handles(); }

private:
  uv_loop_t raw_{};
};

inline loop_view default_loop() noexcept {
  return loop_view{uv_default_loop()};
}

template<class F>
void loop_view::walk(F &&callback) {
  using Fn = std::remove_reference_t<F>;
  uv_walk(raw_, [](uv_handle_t *h, void *arg) noexcept {
    detail::invoke_callback(*static_cast<Fn *>(arg), handle_view{h});
  }, std::addressof(callback));
}

inline std::vector<handle_view> loop_view::handles() {
  std::vector<handle_view> result;
  walk([&](handle_view h) { result.push_back(h); });
  return result;
}

} // namespace uv
