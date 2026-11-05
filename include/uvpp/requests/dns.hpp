#pragma once

#include <algorithm>
#include <cstddef>
#include <concepts>
#include <cstring>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/net/socket_address.hpp"
#include "uvpp/requests/request.hpp"

namespace uv {

  struct address_info {
    int flags = 0;
    int family = 0;
    int socket_type = 0;
    int protocol = 0;
    std::size_t address_length = 0;
    socket_address address;
    std::string canonical_name;
  };

  namespace detail {

    inline socket_address copy_socket_address(const sockaddr *address, std::size_t length) noexcept {
      socket_address out;
      *out.native_len() = 0;

      if (!address || length == 0) {
        return out;
      }

      const auto bounded = std::min(length, sizeof(sockaddr_storage));
      std::memcpy(out.native(), address, bounded);
      *out.native_len() = static_cast<int>(bounded);
      return out;
    }

  }

  namespace raw {

  class addrinfo_view {
  public:
    addrinfo_view() = default;
    explicit addrinfo_view(const addrinfo *raw) noexcept : raw_{raw} {}

    const addrinfo *native() const noexcept { return raw_; }

    int flags() const noexcept { return raw_ ? raw_->ai_flags : 0; }
    int family() const noexcept { return raw_ ? raw_->ai_family : 0; }
    int socket_type() const noexcept { return raw_ ? raw_->ai_socktype : 0; }
    int protocol() const noexcept { return raw_ ? raw_->ai_protocol : 0; }
    std::size_t address_length() const noexcept {
      return raw_ ? static_cast<std::size_t>(raw_->ai_addrlen) : 0;
    }
    const sockaddr *address() const noexcept { return raw_ ? raw_->ai_addr : nullptr; }
    std::string_view canonical_name() const noexcept {
      return raw_ && raw_->ai_canonname ? std::string_view{raw_->ai_canonname} : std::string_view{};
    }

    address_info to_value() const {
      address_info out;

      if (!raw_) {
        return out;
      }

      out.flags = raw_->ai_flags;
      out.family = raw_->ai_family;
      out.socket_type = raw_->ai_socktype;
      out.protocol = raw_->ai_protocol;
      out.address_length = address_length();
      out.address = detail::copy_socket_address(raw_->ai_addr, out.address_length);
      out.canonical_name = raw_->ai_canonname ? raw_->ai_canonname : "";
      return out;
    }

  private:
    const addrinfo *raw_ = nullptr;
  };

  class addrinfo_iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = addrinfo_view;
    using difference_type = std::ptrdiff_t;
    using pointer = void;
    using reference = addrinfo_view;

    addrinfo_iterator() = default;
    explicit addrinfo_iterator(const addrinfo *raw) noexcept : raw_{raw} {}

    addrinfo_view operator*() const noexcept { return addrinfo_view{raw_}; }

    addrinfo_iterator &operator++() noexcept {
      raw_ = raw_ ? raw_->ai_next : nullptr;
      return *this;
    }

    addrinfo_iterator operator++(int) noexcept {
      auto copy = *this;
      ++(*this);
      return copy;
    }

    friend bool operator==(addrinfo_iterator lhs, addrinfo_iterator rhs) noexcept {
      return lhs.raw_ == rhs.raw_;
    }

    friend bool operator!=(addrinfo_iterator lhs, addrinfo_iterator rhs) noexcept {
      return !(lhs == rhs);
    }

  private:
    const addrinfo *raw_ = nullptr;
  };

  class getaddrinfo_result {
  public:
    getaddrinfo_result() = default;
    getaddrinfo_result(int status, addrinfo *addresses) noexcept
      : status_{status}, addresses_{addresses} {}

    getaddrinfo_result(const getaddrinfo_result&) = delete;
    getaddrinfo_result& operator=(const getaddrinfo_result&) = delete;

    getaddrinfo_result(getaddrinfo_result &&other) noexcept
      : status_{other.status_}, addresses_{std::exchange(other.addresses_, nullptr)} {}

    getaddrinfo_result &operator=(getaddrinfo_result &&other) noexcept {
      if (this != &other) {
        reset();
        status_ = other.status_;
        addresses_ = std::exchange(other.addresses_, nullptr);
      }
      return *this;
    }

    ~getaddrinfo_result() {
      reset();
    }

    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    uv::status status() const noexcept { return uv::status::from_native(status_); }
    int raw_status() const noexcept { return status_; }
    uv::error_code error_code() const noexcept { return make_error_code(status_); }

    addrinfo *native() noexcept { return addresses_; }
    const addrinfo *native() const noexcept { return addresses_; }

    addrinfo_iterator begin() const noexcept { return addrinfo_iterator{addresses_}; }
    addrinfo_iterator end() const noexcept { return addrinfo_iterator{}; }

    std::vector<address_info> to_vector() const {
      std::vector<address_info> out;

      for (addrinfo_view entry : *this) {
        out.push_back(entry.to_value());
      }

      return out;
    }

    void reset(addrinfo *addresses = nullptr) noexcept {
      if (addresses_) {
        uv_freeaddrinfo(addresses_);
      }
      addresses_ = addresses;
    }

  private:
    int status_ = 0;
    addrinfo *addresses_ = nullptr;
  };

  class getnameinfo_result {
  public:
    getnameinfo_result(int status, const char *hostname, const char *service) noexcept
      : status_{status}, hostname_{hostname}, service_{service} {}
    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    uv::status status() const noexcept { return uv::status::from_native(status_); }
    int raw_status() const noexcept { return status_; }
    uv::error_code error_code() const noexcept { return make_error_code(status_); }
    std::string_view hostname() const noexcept { return hostname_ ? hostname_ : ""; }
    std::string_view service() const noexcept { return service_ ? service_ : ""; }
    const char *native_hostname() const noexcept { return hostname_; }
    const char *native_service() const noexcept { return service_; }
  private:
    int status_;
    const char *hostname_;
    const char *service_;
  };

  class getaddrinfo_request final : public basic_request<getaddrinfo_request, uv_getaddrinfo_t> {
  public:
    using callback = std::function<void(getaddrinfo_request&, getaddrinfo_result)>;
    bool pending() const noexcept { return pending_; }
    uv::status cancel() noexcept {
      return pending_ ? uv::status::from_native(uv_cancel(native_request())) : uv::status::from_native(UV_EINVAL);
    }

    uv::status submit(loop_view loop, const char *node, const char *service, const addrinfo *hints, callback cb) {
      if (pending_) return uv::status::from_native(UV_EBUSY);
      if (!cb) return uv::status::from_native(UV_EINVAL);
      const int rc = uv_getaddrinfo(loop.native(), native(), &getaddrinfo_request::complete, node, service, hints);
      if (rc >= 0) { callback_ = std::move(cb); static_callback_ = nullptr; pending_ = true; }
      return uv::status::from_native(rc);
    }
    template<auto Callback>
      requires std::invocable<decltype(Callback), getaddrinfo_request&, getaddrinfo_result>
    uv::status submit_static(loop_view loop, const char *node, const char *service, const addrinfo *hints) noexcept {
      if (pending_) return uv::status::from_native(UV_EBUSY);
      const int rc = uv_getaddrinfo(loop.native(), native(), &getaddrinfo_request::complete, node, service, hints);
      if (rc >= 0) {
        callback_ = {};
        static_callback_ = [](getaddrinfo_request &req, getaddrinfo_result result) noexcept {
          detail::invoke_static_callback<Callback>(req, std::move(result));
        };
        pending_ = true;
      }
      return uv::status::from_native(rc);
    }
  private:
    static void complete(uv_getaddrinfo_t *raw, int code, addrinfo *addresses) noexcept {
      auto &self = from_native(raw);
      auto callback = std::move(self.callback_);
      self.callback_ = {};
      auto static_callback = std::exchange(self.static_callback_, nullptr);
      self.pending_ = false;
      getaddrinfo_result result{code, addresses};
      if (static_callback) static_callback(self, std::move(result));
      else if (callback) detail::invoke_callback(callback, self, std::move(result));
    }
    callback callback_{};
    void (*static_callback_)(getaddrinfo_request&, getaddrinfo_result) noexcept = nullptr;
    bool pending_ = false;
  };

  class getnameinfo_request final : public basic_request<getnameinfo_request, uv_getnameinfo_t> {
  public:
    using callback = std::function<void(getnameinfo_request&, getnameinfo_result)>;
    bool pending() const noexcept { return pending_; }
    uv::status cancel() noexcept {
      return pending_ ? uv::status::from_native(uv_cancel(native_request())) : uv::status::from_native(UV_EINVAL);
    }
    uv::status submit(loop_view loop, const sockaddr *address, int flags, callback cb) {
      if (pending_) return uv::status::from_native(UV_EBUSY);
      if (!cb) return uv::status::from_native(UV_EINVAL);
      const int rc = uv_getnameinfo(loop.native(), native(), &getnameinfo_request::complete, address, flags);
      if (rc >= 0) { callback_ = std::move(cb); static_callback_ = nullptr; pending_ = true; }
      return uv::status::from_native(rc);
    }
    template<auto Callback>
      requires std::invocable<decltype(Callback), getnameinfo_request&, getnameinfo_result>
    uv::status submit_static(loop_view loop, const sockaddr *address, int flags) noexcept {
      if (pending_) return uv::status::from_native(UV_EBUSY);
      const int rc = uv_getnameinfo(loop.native(), native(), &getnameinfo_request::complete, address, flags);
      if (rc >= 0) {
        callback_ = {};
        static_callback_ = [](getnameinfo_request &req, getnameinfo_result result) noexcept {
          detail::invoke_static_callback<Callback>(req, result);
        };
        pending_ = true;
      }
      return uv::status::from_native(rc);
    }
  private:
    static void complete(uv_getnameinfo_t *raw, int code, const char *hostname, const char *service) noexcept {
      auto &self = from_native(raw);
      auto callback = std::move(self.callback_);
      self.callback_ = {};
      auto static_callback = std::exchange(self.static_callback_, nullptr);
      self.pending_ = false;
      getnameinfo_result result{code, hostname, service};
      if (static_callback) static_callback(self, result);
      else if (callback) detail::invoke_callback(callback, self, result);
    }
    callback callback_{};
    void (*static_callback_)(getnameinfo_request&, getnameinfo_result) noexcept = nullptr;
    bool pending_ = false;
  };

  } // namespace raw
}
