#pragma once

#include <type_traits>

#include <uv.h>

namespace uvpp::detail {

  template<class Derived, class Raw, class BaseRaw>
  class native_storage {
  public:
    Raw *native() noexcept { return &raw_; }
    const Raw *native() const noexcept { return &raw_; }

    BaseRaw *native_base() noexcept {
      return reinterpret_cast<BaseRaw *>(&raw_);
    }

    const BaseRaw *native_base() const noexcept {
      return reinterpret_cast<const BaseRaw *>(&raw_);
    }

    static Derived &from_native(Raw *raw) noexcept {
      static_assert(std::is_standard_layout_v<native_storage>);
      auto *storage = reinterpret_cast<native_storage *>(raw);
      return *static_cast<Derived *>(storage);
    }

    static const Derived &from_native(const Raw *raw) noexcept {
      static_assert(std::is_standard_layout_v<native_storage>);
      auto *storage = reinterpret_cast<const native_storage *>(raw);
      return *static_cast<const Derived *>(storage);
    }

    static Derived &from_native(BaseRaw *raw) noexcept {
      return from_native(reinterpret_cast<Raw *>(raw));
    }

    static const Derived &from_native(const BaseRaw *raw) noexcept {
      return from_native(reinterpret_cast<const Raw *>(raw));
    }

    template<class T>
    void user_data(T *data) noexcept {
      native_base()->data = data;
    }

    template<class T>
    void user_data(T &data) noexcept {
      user_data(&data);
    }

    template<class T>
    T *user_data() noexcept {
      return static_cast<T *>(native_base()->data);
    }

    template<class T>
    const T *user_data() const noexcept {
      return static_cast<const T *>(native_base()->data);
    }

    void clear_user_data() noexcept {
      native_base()->data = nullptr;
    }

  protected:
    native_storage() = default;
    ~native_storage() = default;

  private:
    Raw raw_{};
  };

}
