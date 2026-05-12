#pragma once

#include <system_error>

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

  inline std::error_code make_error_code(int status) noexcept {
    return status < 0 ? std::error_code(status, category()) : std::error_code{};
  }

  class error : public std::system_error {
  public:
    explicit error(int status)
      : std::system_error(make_error_code(status), uv_err_name(status)) {}
  };

  inline int throw_if_error(int status) {
    if (status < 0) {
      throw error(status);
    }
    return status;
  }

  class result {
  public:
    result() = default;
    explicit result(int status) : status_{status} {}

    bool ok() const noexcept { return status_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    int status() const noexcept { return status_; }

    std::error_code error_code() const noexcept {
      return make_error_code(status_);
    }

  private:
    int status_ = 0;
  };

}

