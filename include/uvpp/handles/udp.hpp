#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/handles/handle.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/buffer.hpp"
#include "uvpp/net/socket_address.hpp"
#include "uvpp/requests/udp_send.hpp"

namespace uv {

  class udp_receive_result {
  public:
    udp_receive_result(ssize_t nread, const uv_buf_t *buf, const sockaddr *addr, unsigned flags) noexcept
      : nread_{nread}, buffer_{buf ? buffer_view::from_native(*buf) : buffer_view{}}, addr_{addr}, flags_{flags} {}

    bool ok() const noexcept { return nread_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    bool empty() const noexcept { return nread_ == 0; }
    bool empty_event() const noexcept { return nread_ == 0 && addr_ == nullptr; }
    bool partial() const noexcept { return (flags_ & UV_UDP_PARTIAL) != 0; }
#if UVPP_HAS_UDP_MMSG_CHUNK
    bool mmsg_chunk() const noexcept { return (flags_ & UV_UDP_MMSG_CHUNK) != 0; }
#else
    bool mmsg_chunk() const noexcept { return false; }
#endif
#if UVPP_HAS_UDP_MMSG_FREE
    bool mmsg_free() const noexcept { return (flags_ & UV_UDP_MMSG_FREE) != 0; }
#else
    bool mmsg_free() const noexcept { return false; }
#endif
    ssize_t count() const noexcept { return nread_; }
    result status() const noexcept { return result{static_cast<int>(nread_)}; }
    unsigned flags() const noexcept { return flags_; }
    const sockaddr *address() const noexcept { return addr_; }

    std::span<const std::byte> bytes() const noexcept {
      if (nread_ <= 0) {
        return {};
      }

      return std::as_bytes(std::span{buffer_.data(), static_cast<std::size_t>(nread_)});
    }

    std::span<char> storage() const noexcept { return buffer_.chars(); }
    const buffer_view &raw_buffer() const noexcept { return buffer_; }

  private:
    ssize_t nread_;
    buffer_view buffer_;
    const sockaddr *addr_;
    unsigned flags_;
  };

  enum class membership {
    leave = UV_LEAVE_GROUP,
    join = UV_JOIN_GROUP
  };

  enum class udp_socket_family : unsigned int {
    ipv4 = AF_INET,
    ipv6 = AF_INET6
  };

  enum class udp_init_flag : unsigned int {
    none = 0
#if UVPP_HAS_UDP_RECVMMSG_FLAG
    ,
    recvmmsg = UV_UDP_RECVMMSG
#endif
  };

  constexpr unsigned int operator|(udp_socket_family family, udp_init_flag flag) noexcept {
    return static_cast<unsigned int>(family) | static_cast<unsigned int>(flag);
  }

  constexpr unsigned int operator|(udp_init_flag lhs, udp_init_flag rhs) noexcept {
    return static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs);
  }

  constexpr unsigned int operator|(unsigned int lhs, udp_init_flag rhs) noexcept {
    return lhs | static_cast<unsigned int>(rhs);
  }

  enum class udp_bind_flag : unsigned int {
    ipv6_only = UV_UDP_IPV6ONLY,
    reuse_address = UV_UDP_REUSEADDR
#if UVPP_HAS_UDP_LINUX_RECVERR
    ,
    linux_receive_error = UV_UDP_LINUX_RECVERR
#endif
#if UVPP_HAS_UDP_REUSEPORT
    ,
    reuse_port = UV_UDP_REUSEPORT
#endif
  };

  constexpr unsigned int operator|(udp_bind_flag lhs, udp_bind_flag rhs) noexcept {
    return static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs);
  }

  constexpr unsigned int operator|(unsigned int lhs, udp_bind_flag rhs) noexcept {
    return lhs | static_cast<unsigned int>(rhs);
  }

  class send_now_result {
  public:
    explicit send_now_result(int value) noexcept : value_{value} {}

    bool ok()          const noexcept { return value_ >= 0; }
    bool would_block() const noexcept { return value_ == UV_EAGAIN; }
    bool has_error()   const noexcept { return value_ < 0 && value_ != UV_EAGAIN; }

    std::size_t bytes_sent() const noexcept {
      return ok() ? static_cast<std::size_t>(value_) : 0;
    }

    int raw() const noexcept { return value_; }

    std::error_code error_code() const noexcept {
      return has_error() ? make_error_code(value_) : std::error_code{};
    }

  private:
    int value_;
  };

#if UVPP_HAS_UDP_TRY_SEND2
  class send_many_now_result {
  public:
    explicit send_many_now_result(int value) noexcept : value_{value} {}

    bool ok()          const noexcept { return value_ >= 0; }
    bool would_block() const noexcept { return value_ == UV_EAGAIN; }
    bool has_error()   const noexcept { return value_ < 0 && value_ != UV_EAGAIN; }

    std::size_t datagrams_sent() const noexcept {
      return ok() ? static_cast<std::size_t>(value_) : 0;
    }

    int raw() const noexcept { return value_; }

    std::error_code error_code() const noexcept {
      return has_error() ? make_error_code(value_) : std::error_code{};
    }

  private:
    int value_;
  };

  class udp_send_many_view {
  public:
    udp_send_many_view(unsigned int count, uv_buf_t **buffers, unsigned int *buffer_counts,
                       sockaddr **addresses) noexcept
      : count_{count}, buffers_{buffers}, buffer_counts_{buffer_counts}, addresses_{addresses} {}

    udp_send_many_view(std::span<uv_buf_t *> buffers, std::span<unsigned int> buffer_counts,
                       std::span<sockaddr *> addresses) noexcept
      : udp_send_many_view{static_cast<unsigned int>(buffers.size()), buffers.data(), buffer_counts.data(),
                           addresses.data()} {
      assert(buffers.size() == buffer_counts.size());
      assert(buffers.size() == addresses.size());
      if (buffers.size() != buffer_counts.size() || buffers.size() != addresses.size()) {
        count_ = 0;
      }
    }

    unsigned int count() const noexcept { return count_; }
    uv_buf_t **buffers() const noexcept { return buffers_; }
    unsigned int *buffer_counts() const noexcept { return buffer_counts_; }
    sockaddr **addresses() const noexcept { return addresses_; }

  private:
    unsigned int count_;
    uv_buf_t **buffers_;
    unsigned int *buffer_counts_;
    sockaddr **addresses_;
  };
#endif

  class udp final : public basic_handle<udp, uv_udp_t> {
  public:
    using allocate_callback = std::function<buffer_view(udp&, std::size_t)>;
    using receive_callback = std::function<void(udp&, udp_receive_result)>;

    explicit udp(loop &l) {
      throw_if_error(uv_udp_init(l.native(), native()));
    }

    explicit udp(loop_view l) {
      throw_if_error(uv_udp_init(l.native(), native()));
    }

#if UVPP_HAS_UDP_INIT_EX
    udp(loop &l, udp_socket_family family) {
      throw_if_error(uv_udp_init_ex(l.native(), native(), static_cast<unsigned int>(family)));
    }

    udp(loop_view l, udp_socket_family family) {
      throw_if_error(uv_udp_init_ex(l.native(), native(), static_cast<unsigned int>(family)));
    }
#endif

#if UVPP_HAS_UDP_INIT_EX && UVPP_HAS_UDP_RECVMMSG_FLAG
    udp(loop &l, udp_socket_family family, udp_init_flag flag) {
      throw_if_error(uv_udp_init_ex(l.native(), native(), family | flag));
    }

    udp(loop_view l, udp_socket_family family, udp_init_flag flag) {
      throw_if_error(uv_udp_init_ex(l.native(), native(), family | flag));
    }
#endif

    void open(uv_os_sock_t socket) {
      throw_if_error(uv_udp_open(native(), socket));
    }

    void bind(const ipv4 &addr, unsigned int flags = 0) {
      throw_if_error(uv_udp_bind(native(), addr.native_sockaddr(), flags));
    }

    void bind(const ipv6 &addr, unsigned int flags = 0) {
      throw_if_error(uv_udp_bind(native(), addr.native_sockaddr(), flags));
    }

    void bind(const ipv4 &addr, udp_bind_flag flag) {
      bind(addr, static_cast<unsigned int>(flag));
    }

    void bind(const ipv6 &addr, udp_bind_flag flag) {
      bind(addr, static_cast<unsigned int>(flag));
    }

#if UVPP_HAS_UDP_CONNECT
    void connect(const ipv4 &addr) {
      throw_if_error(uv_udp_connect(native(), addr.native_sockaddr()));
    }

    void connect(const ipv6 &addr) {
      throw_if_error(uv_udp_connect(native(), addr.native_sockaddr()));
    }

    void disconnect() {
      throw_if_error(uv_udp_connect(native(), nullptr));
    }
#endif

    socket_address sockname() {
      socket_address addr;
      throw_if_error(uv_udp_getsockname(native(), addr.native(), addr.native_len()));
      return addr;
    }

#if UVPP_HAS_UDP_CONNECT
    socket_address peername() {
      socket_address addr;
      throw_if_error(uv_udp_getpeername(native(), addr.native(), addr.native_len()));
      return addr;
    }
#endif

    void receive_start(allocate_callback allocator, receive_callback receiver) {
      allocate_callback_ = std::move(allocator);
      receive_callback_ = std::move(receiver);
      throw_if_error(uv_udp_recv_start(native(), &udp::alloc_trampoline, &udp::receive_trampoline));
    }

    void receive_stop() {
      throw_if_error(uv_udp_recv_stop(native()));
    }

    void send(udp_send_request &request, std::span<const buffer_view> buffers, const sockaddr *addr,
              udp_send_request::callback callback) {
      detail::submit_request(request, std::move(callback), [&] {
        return uv_udp_send(request.native(), native(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                           static_cast<unsigned int>(buffers.size()), addr, &udp::send_trampoline);
      });
    }

    void send(udp_send_request &request, std::span<const buffer_view> buffers, const ipv4 &addr,
              udp_send_request::callback callback) {
      send(request, buffers, addr.native_sockaddr(), std::move(callback));
    }

    void send(udp_send_request &request, std::span<const buffer_view> buffers, const ipv6 &addr,
              udp_send_request::callback callback) {
      send(request, buffers, addr.native_sockaddr(), std::move(callback));
    }

    void send(udp_send_request &request, const buffer_view &buf, const sockaddr *addr,
              udp_send_request::callback callback) {
      send(request, std::span<const buffer_view>{&buf, 1}, addr, std::move(callback));
    }

    void send(udp_send_request &request, const buffer_view &buf, const ipv4 &addr,
              udp_send_request::callback callback) {
      send(request, std::span<const buffer_view>{&buf, 1}, addr, std::move(callback));
    }

    void send(udp_send_request &request, const buffer_view &buf, const ipv6 &addr,
              udp_send_request::callback callback) {
      send(request, std::span<const buffer_view>{&buf, 1}, addr, std::move(callback));
    }

    void send(udp_send_request &request, std::span<const std::byte> bytes, const sockaddr *addr,
              udp_send_request::callback callback) {
      auto raw = uv_buf_init(const_cast<char *>(reinterpret_cast<const char *>(bytes.data())),
                             static_cast<unsigned int>(bytes.size()));
      detail::submit_request(request, std::move(callback), [&] {
        return uv_udp_send(request.native(), native(), &raw, 1, addr, &udp::send_trampoline);
      });
    }

    void send(udp_send_request &request, std::span<const std::byte> bytes, const ipv4 &addr,
              udp_send_request::callback callback) {
      send(request, bytes, addr.native_sockaddr(), std::move(callback));
    }

    void send(udp_send_request &request, std::span<const std::byte> bytes, const ipv6 &addr,
              udp_send_request::callback callback) {
      send(request, bytes, addr.native_sockaddr(), std::move(callback));
    }

    template<auto Callback>
    void send_static(udp_send_request &request, std::span<const buffer_view> buffers, const sockaddr *addr) {
      throw_if_error(uv_udp_send(request.native(), native(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                 static_cast<unsigned int>(buffers.size()), addr, [](uv_udp_send_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(udp_send_request::from_native(raw), result{status});
      }));
    }

    template<auto Callback>
    void send_static(udp_send_request &request, const buffer_view &buf, const ipv4 &addr) {
      send_static<Callback>(request, std::span<const buffer_view>{&buf, 1}, addr.native_sockaddr());
    }

    template<auto Callback>
    void send_static(udp_send_request &request, const buffer_view &buf, const ipv6 &addr) {
      send_static<Callback>(request, std::span<const buffer_view>{&buf, 1}, addr.native_sockaddr());
    }

    send_now_result send_now(std::span<const buffer_view> buffers, const sockaddr *addr) noexcept {
      return send_now_result{uv_udp_try_send(native(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                             static_cast<unsigned int>(buffers.size()), addr)};
    }

    send_now_result send_now(const buffer_view &buf, const ipv4 &addr) noexcept {
      return send_now(std::span<const buffer_view>{&buf, 1}, addr.native_sockaddr());
    }

    send_now_result send_now(const buffer_view &buf, const ipv6 &addr) noexcept {
      return send_now(std::span<const buffer_view>{&buf, 1}, addr.native_sockaddr());
    }

    send_now_result send_now(std::span<const buffer_view> buffers) noexcept {
      return send_now_result{uv_udp_try_send(native(), reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                             static_cast<unsigned int>(buffers.size()), nullptr)};
    }

    send_now_result send_now(const buffer_view &buf) noexcept {
      return send_now(std::span<const buffer_view>{&buf, 1});
    }

    send_now_result send_now(std::span<const std::byte> bytes, const sockaddr *addr) noexcept {
      auto raw = uv_buf_init(const_cast<char *>(reinterpret_cast<const char *>(bytes.data())),
                             static_cast<unsigned int>(bytes.size()));
      return send_now_result{uv_udp_try_send(native(), &raw, 1, addr)};
    }

    send_now_result send_now(std::span<const std::byte> bytes, const ipv4 &addr) noexcept {
      return send_now(bytes, addr.native_sockaddr());
    }

    send_now_result send_now(std::span<const std::byte> bytes, const ipv6 &addr) noexcept {
      return send_now(bytes, addr.native_sockaddr());
    }

    send_now_result send_now(std::span<const std::byte> bytes) noexcept {
      return send_now(bytes, static_cast<const sockaddr *>(nullptr));
    }

#if UVPP_HAS_UDP_TRY_SEND2
    send_many_now_result send_many_now(udp_send_many_view batch) noexcept {
      return send_many_now_result{uv_udp_try_send2(native(), batch.count(), batch.buffers(),
                                                   batch.buffer_counts(), batch.addresses(), 0)};
    }

    send_many_now_result send_many_now(unsigned int count, uv_buf_t **buffers,
                                       unsigned int *buffer_counts, sockaddr **addresses) noexcept {
      return send_many_now(udp_send_many_view{count, buffers, buffer_counts, addresses});
    }
#endif

    std::size_t send_queue_size() const noexcept {
      return uv_udp_get_send_queue_size(native());
    }

    std::size_t send_queue_count() const noexcept {
      return uv_udp_get_send_queue_count(native());
    }

    void set_membership(std::string_view multicast_addr, std::string_view interface_addr, membership m) {
      std::string multicast{multicast_addr};
      std::string interface{interface_addr};
      throw_if_error(uv_udp_set_membership(native(), multicast.c_str(), interface.c_str(),
                                           static_cast<uv_membership>(m)));
    }

    void set_membership(std::string_view multicast_addr, membership m) {
      std::string multicast{multicast_addr};
      throw_if_error(uv_udp_set_membership(native(), multicast.c_str(), nullptr, static_cast<uv_membership>(m)));
    }

#if UVPP_HAS_UDP_SOURCE_MEMBERSHIP
    void set_source_membership(std::string_view multicast_addr, std::string_view interface_addr,
                               std::string_view source_addr, membership m) {
      std::string multicast{multicast_addr};
      std::string interface{interface_addr};
      std::string source{source_addr};
      throw_if_error(uv_udp_set_source_membership(native(), multicast.c_str(), interface.c_str(),
                                                  source.c_str(), static_cast<uv_membership>(m)));
    }

    void set_source_membership(std::string_view multicast_addr, std::string_view source_addr,
                               membership m) {
      std::string multicast{multicast_addr};
      std::string source{source_addr};
      throw_if_error(uv_udp_set_source_membership(native(), multicast.c_str(), nullptr,
                                                  source.c_str(), static_cast<uv_membership>(m)));
    }
#endif

#if UVPP_HAS_UDP_USING_RECVMMSG
    bool using_recvmmsg() const noexcept {
      return uv_udp_using_recvmmsg(native()) != 0;
    }
#endif

    void set_multicast_loop(bool enable) {
      throw_if_error(uv_udp_set_multicast_loop(native(), enable ? 1 : 0));
    }

    void set_multicast_ttl(int ttl) {
      throw_if_error(uv_udp_set_multicast_ttl(native(), ttl));
    }

    void set_multicast_interface(std::string_view interface_addr) {
      std::string interface{interface_addr};
      throw_if_error(uv_udp_set_multicast_interface(native(), interface.c_str()));
    }

    void set_multicast_interface() {
      throw_if_error(uv_udp_set_multicast_interface(native(), nullptr));
    }

    void set_broadcast(bool enable) {
      throw_if_error(uv_udp_set_broadcast(native(), enable ? 1 : 0));
    }

    void set_ttl(int ttl) {
      throw_if_error(uv_udp_set_ttl(native(), ttl));
    }

  private:
    static void alloc_trampoline(uv_handle_t *raw, size_t suggested_size, uv_buf_t *buf) noexcept {
      auto &self = udp::from_native(raw);

      if (self.allocate_callback_) {
        detail::invoke_callback([&] {
          auto out = self.allocate_callback_(self, suggested_size);
          *buf = *out.native();
        });
      } else {
        *buf = uv_buf_init(nullptr, 0);
      }
    }

    static void receive_trampoline(uv_udp_t *raw, ssize_t nread, const uv_buf_t *buf, const sockaddr *addr,
                                   unsigned flags) noexcept {
      auto &self = udp::from_native(raw);
      if (self.receive_callback_) {
        detail::invoke_callback(self.receive_callback_, self, udp_receive_result{nread, buf, addr, flags});
      }
    }

    static void send_trampoline(uv_udp_send_t *raw, int status) noexcept {
      udp_send_request::from_native(raw).invoke(status);
    }

    allocate_callback allocate_callback_{};
    receive_callback receive_callback_{};
  };

}
