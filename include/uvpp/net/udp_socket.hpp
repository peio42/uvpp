#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/buffer.hpp"
#include "uvpp/net/socket_address.hpp"

namespace uv {

class udp_socket;
class udp_socket_view;

namespace detail {

enum class udp_close_phase { open, closing, closed };

struct udp_socket_access { udp_socket *socket = nullptr; };
[[nodiscard]] udp_socket_view make_udp_socket_view(
    std::shared_ptr<udp_socket_access>) noexcept;

struct udp_socket_state {
  uv_udp_t udp{};
  uv::loop *loop = nullptr;
  udp_close_phase close_phase = udp_close_phase::open;
  std::vector<std::coroutine_handle<>> close_waiters{};
  std::coroutine_handle<> callback_close_waiter{};
  bool owner_released = false;
  bool close_callback_active = false;
  bool send_active = false;
  void *active_receive = nullptr;

  static udp_socket_state &from_handle(uv_handle_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<udp_socket_state *>(bytes - offsetof(udp_socket_state, udp));
  }
  bool closing() const noexcept { return close_phase != udp_close_phase::open; }
  bool request_close() noexcept {
    if (close_phase != udp_close_phase::open) return false;
    close_phase = udp_close_phase::closing;
    uv_close(reinterpret_cast<uv_handle_t *>(&udp), &udp_socket_state::on_close);
    return true;
  }
  bool request_close_and_join(std::coroutine_handle<> continuation) {
    assert(close_phase != udp_close_phase::closed);
    if (close_phase == udp_close_phase::closed) return false;
    close_waiters.push_back(continuation);
    return request_close();
  }
  void release_owner() noexcept {
    assert(!owner_released);
    owner_released = true;
    if (close_phase == udp_close_phase::open) {
      (void)request_close();
    } else if (close_phase == udp_close_phase::closed && !close_callback_active) {
      delete this;
    }
  }
  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(raw);
    self.close_phase = udp_close_phase::closed;
    self.close_callback_active = true;
    auto callback_waiter = std::exchange(self.callback_close_waiter, {});
    auto waiters = std::move(self.close_waiters);
    if (callback_waiter) callback_waiter.resume();
    for (auto continuation : waiters) if (continuation) continuation.resume();
    self.close_callback_active = false;
    if (self.owner_released) delete &self;
  }
};

class udp_close_completion {
public:
  explicit udp_close_completion(udp_socket_state *state) noexcept : state_{state} {}
  bool await_ready() const noexcept { return false; }
  template<class Promise>
    requires std::derived_from<Promise, co::detail::task_promise_base>
  bool await_suspend(std::coroutine_handle<Promise> continuation) {
    if (state_ == nullptr) { status_ = UV_EBADF; return false; }
    if (&continuation.promise().execution_loop() != state_->loop) {
      throw std::logic_error{"uv internal udp close used from a different loop"};
    }
    if (state_->close_phase == udp_close_phase::closed) return false;
    initiated_ = state_->request_close_and_join(continuation);
    return true;
  }
  void await_resume() { throw_if_error(status_); }
  bool initiated_close() const noexcept { return initiated_; }
private:
  udp_socket_state *state_ = nullptr;
  int status_ = 0;
  bool initiated_ = false;
};

[[nodiscard]] udp_close_completion close_completion(udp_socket &) noexcept;

} // namespace detail

class udp_socket {
public:
  udp_socket(const udp_socket &) = delete;
  udp_socket &operator=(const udp_socket &) = delete;
  udp_socket(udp_socket &&other) noexcept : state_{std::move(other.state_)} {}
  udp_socket &operator=(udp_socket &&other) noexcept {
    if (this != &other) { reset(); state_ = std::move(other.state_); }
    return *this;
  }
  udp_socket(uv::loop &loop, const ipv4 &address) { initialize(loop, address.native_sockaddr()); }
  udp_socket(uv::loop &loop, const ipv6 &address) { initialize(loop, address.native_sockaddr()); }
  ~udp_socket() { reset(); }

  uv_udp_t *native_handle() noexcept { return state_ ? &state_->udp : nullptr; }
  const uv_udp_t *native_handle() const noexcept { return state_ ? &state_->udp : nullptr; }
  bool closing() const noexcept { return state_ && state_->closing(); }
  bool has_execution_loop(const uv::loop &loop) const noexcept { return state_ && state_->loop == &loop; }
  bool has_active_operation() const noexcept {
    return state_ && (state_->send_active || state_->active_receive != nullptr);
  }
  socket_address sockname() const {
    if (!state_) throw_if_error(UV_EBADF);
    socket_address address;
    throw_if_error(uv_udp_getsockname(&state_->udp, address.native(), address.native_len()));
    return address;
  }
  void close() noexcept { reset(); }

  class recv_from_result {
  public:
    recv_from_result(std::size_t size, const sockaddr *peer, unsigned flags) noexcept
      : size_{size}, flags_{flags} {
      if (peer != nullptr) {
        if (peer->sa_family == AF_INET) std::memcpy(&peer_, peer, sizeof(sockaddr_in));
        if (peer->sa_family == AF_INET6) std::memcpy(&peer_, peer, sizeof(sockaddr_in6));
      }
    }
    std::size_t size() const noexcept { return size_; }
    bool partial() const noexcept { return (flags_ & UV_UDP_PARTIAL) != 0; }
    bool peer_is_v4() const noexcept { return peer_.ss_family == AF_INET; }
    bool peer_is_v6() const noexcept { return peer_.ss_family == AF_INET6; }
    ipv4 peer_v4() const { assert(peer_is_v4()); return ipv4{*reinterpret_cast<const sockaddr_in *>(&peer_)}; }
    ipv6 peer_v6() const { assert(peer_is_v6()); return ipv6{*reinterpret_cast<const sockaddr_in6 *>(&peer_)}; }
  private:
    std::size_t size_ = 0;
    sockaddr_storage peer_{};
    unsigned flags_ = 0;
  };

  class recv_from_awaiter {
  public:
    recv_from_awaiter(detail::udp_socket_state *state, std::span<std::byte> buffer) noexcept
      : state_{state}, buffer_{buffer} {}
    bool await_ready() const noexcept { return false; }
    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->closing()) { status_ = UV_EBADF; return false; }
      if (&continuation.promise().execution_loop() != state_->loop) throw std::logic_error{"uv::udp_socket recv_from used from a different loop"};
      if (continuation.promise().stop_requested()) { status_ = UV_ECANCELED; return false; }
      if (state_->active_receive != nullptr) { status_ = UV_EBUSY; return false; }
      state_->active_receive = this;
      continuation_ = continuation;
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(registration_, &recv_from_awaiter::on_stop, this)) {
        state_->active_receive = nullptr; continuation_ = {}; status_ = UV_ECANCELED; return false;
      }
      status_ = uv_udp_recv_start(&state_->udp, &recv_from_awaiter::on_alloc, &recv_from_awaiter::on_receive);
      if (status_ < 0) { release_slots(); return false; }
      return true;
    }
    recv_from_result await_resume() {
      throw_if_error(static_cast<int>(status_));
      return recv_from_result{static_cast<std::size_t>(status_),
          reinterpret_cast<const sockaddr *>(&peer_storage_), flags_};
    }
  private:
    static void on_alloc(uv_handle_t *raw, std::size_t, uv_buf_t *out) noexcept {
      // libuv invokes alloc before receive for this exclusive one-shot operation.
      // The active awaiter owns the borrowed caller buffer.
      auto &state = detail::udp_socket_state::from_handle(raw);
      auto *self = static_cast<recv_from_awaiter *>(state.active_receive);
      *out = self == nullptr ? detail::make_native_buffer_unchecked(nullptr, 0)
          : detail::make_native_buffer_unchecked(reinterpret_cast<char *>(self->buffer_.data()),
              self->buffer_.size());
    }
    static void on_receive(uv_udp_t *raw, ssize_t nread, const uv_buf_t *, const sockaddr *peer, unsigned flags) noexcept {
      auto &state = detail::udp_socket_state::from_handle(reinterpret_cast<uv_handle_t *>(raw));
      auto *self = static_cast<recv_from_awaiter *>(state.active_receive);
      if (self == nullptr || (nread == 0 && peer == nullptr)) return;
      self->status_ = nread;
      if (peer != nullptr) {
        if (peer->sa_family == AF_INET) std::memcpy(&self->peer_storage_, peer, sizeof(sockaddr_in));
        if (peer->sa_family == AF_INET6) std::memcpy(&self->peer_storage_, peer, sizeof(sockaddr_in6));
      }
      self->flags_ = flags;
      self->stop_and_resume();
    }
    static void on_stop(void *context) noexcept { static_cast<recv_from_awaiter *>(context)->cancel(); }
    void cancel() noexcept {
      if (state_ == nullptr || state_->active_receive != this) return;
      status_ = UV_ECANCELED;
      stop_and_resume();
    }
    void stop_and_resume() noexcept {
      const int stop = uv_udp_recv_stop(&state_->udp);
      assert(stop >= 0);
      if (stop < 0) std::terminate();
      release_slots();
      auto continuation = std::exchange(continuation_, {});
      continuation.resume();
    }
    void release_slots() noexcept {
      if (state_ != nullptr && state_->active_receive == this) state_->active_receive = nullptr;
      if (cancellation_ != nullptr) cancellation_->unregister(registration_);
      cancellation_ = nullptr;
    }
    detail::udp_socket_state *state_ = nullptr;
    std::span<std::byte> buffer_{};
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration registration_{};
    ssize_t status_ = 0;
    sockaddr_storage peer_storage_{};
    unsigned flags_ = 0;
  };

  class send_to_awaiter {
  public:
    send_to_awaiter(detail::udp_socket_state *state, std::span<const std::byte> data,
        const ipv4 &address) noexcept : state_{state}, buffer_{detail::make_native_buffer_unchecked(
        const_cast<char *>(reinterpret_cast<const char *>(data.data())), data.size())} {
      std::memcpy(&destination_, address.native(), sizeof(sockaddr_in));
    }
    send_to_awaiter(detail::udp_socket_state *state, std::span<const std::byte> data,
        const ipv6 &address) noexcept : state_{state}, buffer_{detail::make_native_buffer_unchecked(
        const_cast<char *>(reinterpret_cast<const char *>(data.data())), data.size())} {
      std::memcpy(&destination_, address.native(), sizeof(sockaddr_in6));
    }
    bool await_ready() const noexcept { return false; }
    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->closing()) { status_ = UV_EBADF; return false; }
      if (&continuation.promise().execution_loop() != state_->loop) throw std::logic_error{"uv::udp_socket send_to used from a different loop"};
      if (continuation.promise().stop_requested()) { status_ = UV_ECANCELED; return false; }
      if (state_->send_active) { status_ = UV_EBUSY; return false; }
      state_->send_active = true;
      continuation_ = continuation;
      status_ = uv_udp_send(&request_, &state_->udp, &buffer_, 1,
          reinterpret_cast<const sockaddr *>(&destination_), &send_to_awaiter::on_send);
      if (status_ < 0) { state_->send_active = false; continuation_ = {}; }
      return status_ >= 0;
    }
    void await_resume() { throw_if_error(status_); }
  private:
    static send_to_awaiter &from_native(uv_udp_send_t *raw) noexcept { return *reinterpret_cast<send_to_awaiter *>(raw); }
    static void on_send(uv_udp_send_t *raw, int status) noexcept {
      auto &self = from_native(raw);
      self.status_ = status;
      self.state_->send_active = false;
      auto continuation = std::exchange(self.continuation_, {});
      continuation.resume();
    }
    uv_udp_send_t request_{};
    detail::udp_socket_state *state_ = nullptr;
    uv_buf_t buffer_{};
    sockaddr_storage destination_{};
    std::coroutine_handle<> continuation_{};
    int status_ = 0;
  };

  [[nodiscard]] recv_from_awaiter recv_from(std::span<std::byte> buffer) noexcept { return {state_.get(), buffer}; }
  [[nodiscard]] send_to_awaiter send_to(std::span<const std::byte> data, const ipv4 &address) {
    detail::check_buffer_length(data.size()); return {state_.get(), data, address};
  }
  [[nodiscard]] send_to_awaiter send_to(std::span<const std::byte> data, const ipv6 &address) {
    detail::check_buffer_length(data.size()); return {state_.get(), data, address};
  }
  [[nodiscard]] send_to_awaiter send_to(std::string_view data, const ipv4 &address) {
    return send_to(std::as_bytes(std::span{data.data(), data.size()}), address);
  }

private:
  void initialize(uv::loop &loop, const sockaddr *address) {
    auto state = std::make_unique<detail::udp_socket_state>();
    state->loop = &loop;
    int status = uv_udp_init(loop.native(), &state->udp);
    if (status < 0) throw_if_error(status);
    status = uv_udp_bind(&state->udp, address, 0);
    if (status < 0) {
      auto *failed = state.release();
      failed->release_owner();
      throw_if_error(status);
    }
    state_ = std::move(state);
  }
  void reset() noexcept {
    if (!state_) return;
    auto *state = state_.release();
    assert(!state->send_active && state->active_receive == nullptr);
    if (state->send_active || state->active_receive != nullptr) std::terminate();
    state->release_owner();
  }
  std::unique_ptr<detail::udp_socket_state> state_{};
  friend class udp_socket_view;
  friend detail::udp_close_completion detail::close_completion(udp_socket &) noexcept;
};

class udp_socket_view {
public:
  socket_address sockname() const { return socket().sockname(); }
  [[nodiscard]] udp_socket::recv_from_awaiter recv_from(std::span<std::byte> buffer) const { return socket().recv_from(buffer); }
  [[nodiscard]] udp_socket::send_to_awaiter send_to(std::span<const std::byte> data, const ipv4 &address) const { return socket().send_to(data, address); }
  [[nodiscard]] udp_socket::send_to_awaiter send_to(std::span<const std::byte> data, const ipv6 &address) const { return socket().send_to(data, address); }
  [[nodiscard]] udp_socket::send_to_awaiter send_to(std::string_view data, const ipv4 &address) const { return socket().send_to(data, address); }
private:
  explicit udp_socket_view(std::shared_ptr<detail::udp_socket_access> access) noexcept : access_{std::move(access)} {}
  udp_socket &socket() const {
    if (!access_ || access_->socket == nullptr) throw std::logic_error{"uv::udp_socket_view used after resource cleanup"};
    return *access_->socket;
  }
  std::shared_ptr<detail::udp_socket_access> access_{};
  friend udp_socket_view detail::make_udp_socket_view(std::shared_ptr<detail::udp_socket_access>) noexcept;
};

namespace detail {
inline udp_socket_view make_udp_socket_view(std::shared_ptr<udp_socket_access> access) noexcept { return udp_socket_view{std::move(access)}; }
inline udp_close_completion close_completion(udp_socket &socket) noexcept { return udp_close_completion{socket.state_.get()}; }
} // namespace detail

} // namespace uv
