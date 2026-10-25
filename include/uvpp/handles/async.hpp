#pragma once

#include <concepts>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/initializable_raw_watcher.hpp"

namespace uv::raw {

  class async final : public ::uv::detail::initializable_raw_watcher_handle<async, uv_async_t> {
    using base = ::uv::detail::initializable_raw_watcher_handle<async, uv_async_t>;

  public:
    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    struct static_callback {};

    async() noexcept = default;

    explicit async(loop &l) : async(l.view()) {}

    explicit async(loop_view l) {
      init(l).value();
    }

    async(loop &l, callback cb) : async(l.view(), std::move(cb)) {}

    async(loop_view l, callback cb) {
      init(l, std::move(cb)).value();
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    async(loop &l, static_callback<Callback> tag) : async(l.view(), tag) {}

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    async(loop_view l, static_callback<Callback> tag) {
      init(l, tag).value();
    }

    status init(loop &l) noexcept { return init(l.view()); }

    status init(loop_view l) noexcept { return init(l, callback{}); }

    status init(loop &l, callback cb) noexcept { return init(l.view(), std::move(cb)); }

    status init(loop_view l, callback cb) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      const auto result = status::from_native(uv_async_init(l.native(), native(), &async::async_trampoline));
      if (!result) return result;
      // Callable preparation precedes native init; committing cannot throw.
      static_dispatch_ = nullptr;
      replace_callback(std::move(cb));
      return result;
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    status init(loop &l, static_callback<Callback> tag) noexcept {
      return init(l.view(), tag);
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    status init(loop_view l, static_callback<Callback>) noexcept {
      const auto result = init(l);
      if (result) static_dispatch_ = &invoke_static<Callback>;
      return result;
    }

    status set_callback(callback cb) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      static_dispatch_ = nullptr;
      replace_callback(std::move(cb));
      return {};
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    status set_callback_static() noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      static_dispatch_ = &invoke_static<Callback>;
      replace_callback({});
      return {};
    }

    // Publish only after init returns; never overlap init and send. The loop
    // sentinel is then immutable, so this read adds no closing-state race.
    // Producers must still be stopped and joined before close; closing() is
    // not a cross-thread lifetime guard.
    status send() noexcept {
      if (!initialized()) return status::from_native(UV_EBADF);
      return status::from_native(uv_async_send(native()));
    }

    void close() noexcept {
      base::close();
      static_dispatch_ = nullptr;
    }

    void close(close_callback cb) noexcept {
      base::close(std::move(cb));
      static_dispatch_ = nullptr;
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    void close_static() noexcept {
      base::template close_static<Callback>();
      static_dispatch_ = nullptr;
    }

  private:
    using static_dispatch = void (*)(async&) noexcept;

    template<auto Callback>
    static void invoke_static(async &self) noexcept {
      ::uv::detail::invoke_static_callback<Callback>(self);
    }

    static void async_trampoline(uv_async_t *raw) noexcept {
      auto &self = async::from_native(raw);
      const auto dispatch = self.static_dispatch_;
      if (dispatch) dispatch(self);
      else self.deliver();
    }

    static_dispatch static_dispatch_ = nullptr;
  };

}
