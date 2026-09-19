#pragma once

#include <cassert>
#include <concepts>
#include <system_error>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <uv.h>

namespace uv {

  class error_category final : public std::error_category {
  public:
    const char *name() const noexcept override {
      return "libuv";
    }

    std::string message(int condition) const override {
      return uv_strerror(condition);
    }
  };

  inline const std::error_category &category() noexcept {
    static const error_category instance;
    return instance;
  }

  // A libuv operational error. A zero code represents no error; non-negative
  // native statuses are normalized to zero because libuv uses negative values
  // for errors.
  class error_code {
  public:
    constexpr error_code() noexcept = default;

    static constexpr error_code from_native(int status) noexcept {
      return error_code{status < 0 ? status : 0, native_tag{}};
    }

    constexpr explicit operator bool() const noexcept { return status_ != 0; }
    constexpr int native() const noexcept { return status_; }

    const char *name() const noexcept { return status_ == 0 ? "" : uv_err_name(status_); }

    std::string message() const {
      return status_ == 0 ? std::string{} : std::string{uv_strerror(status_)};
    }

    explicit operator std::error_code() const noexcept {
      return status_ == 0 ? std::error_code{} : std::error_code{status_, category()};
    }

    friend constexpr bool operator==(error_code, error_code) noexcept = default;

  private:
    struct native_tag {};

    constexpr error_code(int status, native_tag) noexcept : status_{status} {}

    int status_ = 0;
  };

  inline constexpr error_code make_error_code(int status) noexcept {
    return error_code::from_native(status);
  }

  class error : public std::system_error {
  public:
    explicit error(error_code code)
      : std::system_error(static_cast<std::error_code>(code), uv_err_name(code.native())) {
      assert(code);
    }

    explicit error(int status) : error(make_error_code(status)) {}
  };

  inline int throw_if_error(int status) {
    if (status < 0) {
      throw error(status);
    }
    return status;
  }

  template<class T>
  class result {
  public:
    static_assert(!std::is_void_v<T>);

    template<class... Args>
    explicit result(std::in_place_t, Args&&... args)
      : storage_{std::in_place_index<0>, std::forward<Args>(args)...} {}

    explicit result(T value)
      requires (!std::same_as<std::remove_cv_t<T>, error_code>)
      : storage_{std::in_place_index<0>, std::move(value)} {}

    explicit result(error_code code) noexcept : storage_{std::in_place_index<1>, code} {
      assert(code);
    }

    bool has_value() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    T &value() & {
      throw_if_error_value();
      return std::get<0>(storage_);
    }

    const T &value() const & {
      throw_if_error_value();
      return std::get<0>(storage_);
    }

    T &&value() && {
      throw_if_error_value();
      return std::move(std::get<0>(storage_));
    }

    const T &&value() const && {
      throw_if_error_value();
      return std::move(std::get<0>(storage_));
    }

    error_code error() const noexcept {
      return has_value() ? error_code{} : std::get<1>(storage_);
    }

  private:
    void throw_if_error_value() const {
      if (!has_value()) {
        throw uv::error{std::get<1>(storage_)};
      }
    }

    std::variant<T, error_code> storage_;
  };

  template<>
  class result<void> {
  public:
    constexpr result() noexcept = default;

    explicit result(int status) noexcept : error_{make_error_code(status)} {}

    explicit result(error_code code) noexcept : error_{code} {}

    bool has_value() const noexcept { return !error_; }
    explicit operator bool() const noexcept { return has_value(); }

    void value() const {
      if (error_) {
        throw uv::error{error_};
      }
    }

    error_code error() const noexcept { return error_; }

  private:
    error_code error_;
  };

}
