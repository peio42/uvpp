#pragma once

#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/requests/request.hpp"

namespace uv {

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
    result status() const noexcept { return result{status_}; }
    int raw_status() const noexcept { return status_; }
    std::error_code error_code() const noexcept { return make_error_code(status_); }

    addrinfo *native() noexcept { return addresses_; }
    const addrinfo *native() const noexcept { return addresses_; }

    addrinfo_iterator begin() const noexcept { return addrinfo_iterator{addresses_}; }
    addrinfo_iterator end() const noexcept { return addrinfo_iterator{}; }

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
    getnameinfo_result() = default;
    getnameinfo_result(int status, const char *hostname, const char *service)
      : status_{status},
        hostname_{hostname ? hostname : ""},
        service_{service ? service : ""} {}

    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    result status() const noexcept { return result{status_}; }
    int raw_status() const noexcept { return status_; }
    std::error_code error_code() const noexcept { return make_error_code(status_); }

    std::string_view hostname() const noexcept { return hostname_; }
    std::string_view service() const noexcept { return service_; }

  private:
    int status_ = 0;
    std::string hostname_;
    std::string service_;
  };

  class getaddrinfo_request final : public basic_request<getaddrinfo_request, uv_getaddrinfo_t> {
  public:
    using callback = std::function<void(getaddrinfo_request&, getaddrinfo_result)>;

    void set_callback(callback cb) {
      callback_ = std::move(cb);
    }

    void cancel() {
      throw_if_error(uv_cancel(native_request()));
    }

    std::error_code try_cancel() noexcept {
      return make_error_code(uv_cancel(native_request()));
    }

    void set_inputs(std::optional<std::string_view> node, std::optional<std::string_view> service,
                    const addrinfo *hints) {
      node_ = node ? std::optional<std::string>{std::string{*node}} : std::nullopt;
      service_ = service ? std::optional<std::string>{std::string{*service}} : std::nullopt;
      hints_ = hints ? std::optional<addrinfo>{*hints} : std::nullopt;
    }

    void clear_inputs() noexcept {
      node_.reset();
      service_.reset();
      hints_.reset();
    }

    const char *node_arg() const noexcept { return node_ ? node_->c_str() : nullptr; }
    const char *service_arg() const noexcept { return service_ ? service_->c_str() : nullptr; }
    const addrinfo *hints_arg() const noexcept { return hints_ ? &*hints_ : nullptr; }

    void invoke(int status, addrinfo *addresses) noexcept {
      auto callback = std::move(callback_);
      callback_ = {};
      clear_inputs();

      if (callback) {
        detail::invoke_callback(callback, *this, getaddrinfo_result{status, addresses});
      } else if (addresses) {
        uv_freeaddrinfo(addresses);
      }
    }

    template<auto Callback>
    void invoke_static(int status, addrinfo *addresses) noexcept {
      clear_inputs();
      detail::invoke_static_callback<Callback>(*this, getaddrinfo_result{status, addresses});
    }

  private:
    callback callback_{};
    std::optional<std::string> node_;
    std::optional<std::string> service_;
    std::optional<addrinfo> hints_;
  };

  class getnameinfo_request final : public basic_request<getnameinfo_request, uv_getnameinfo_t> {
  public:
    using callback = std::function<void(getnameinfo_request&, getnameinfo_result)>;

    void set_callback(callback cb) {
      callback_ = std::move(cb);
    }

    void cancel() {
      throw_if_error(uv_cancel(native_request()));
    }

    std::error_code try_cancel() noexcept {
      return make_error_code(uv_cancel(native_request()));
    }

    void set_address(const sockaddr *addr) noexcept {
      storage_ = {};
      if (!addr) {
        has_address_ = false;
        return;
      }

      const auto size = addr->sa_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
      std::memcpy(&storage_, addr, size);
      has_address_ = true;
    }

    void clear_address() noexcept {
      storage_ = {};
      has_address_ = false;
    }

    const sockaddr *address_arg() const noexcept {
      return has_address_ ? reinterpret_cast<const sockaddr *>(&storage_) : nullptr;
    }

    void invoke(int status, const char *hostname, const char *service) noexcept {
      auto callback = std::move(callback_);
      callback_ = {};
      clear_address();

      if (callback) {
        detail::invoke_callback(callback, *this, getnameinfo_result{status, hostname, service});
      }
    }

    template<auto Callback>
    void invoke_static(int status, const char *hostname, const char *service) noexcept {
      clear_address();
      detail::invoke_static_callback<Callback>(*this, getnameinfo_result{status, hostname, service});
    }

  private:
    callback callback_{};
    sockaddr_storage storage_{};
    bool has_address_ = false;
  };

}
