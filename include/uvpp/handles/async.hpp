#pragma once

#include <concepts>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/detail/raw_watcher.hpp"

namespace uv::raw {

  class async final : public ::uv::detail::raw_watcher_handle<async, uv_async_t> {
    using base = ::uv::detail::raw_watcher_handle<async, uv_async_t>;

  public:
    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    struct static_callback {};

    explicit async(loop &l) : async(l.view()) {}

    explicit async(loop_view l) {
      init(l.native());
    }

    async(loop &l, callback cb) : async(l.view(), std::move(cb)) {}

    async(loop_view l, callback cb) {
      // The callable is prepared before init. Committing its move after native
      // initialization cannot allocate or throw and needs no native rollback.
      init(l.native());
      replace_callback(std::move(cb));
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    async(loop &l, static_callback<Callback> tag) : async(l.view(), tag) {}

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    async(loop_view l, static_callback<Callback>) : async(l) {
      static_dispatch_ = &invoke_static<Callback>;
    }

    status set_callback(callback cb) noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      static_dispatch_ = nullptr;
      replace_callback(std::move(cb));
      return {};
    }

    template<auto Callback>
      requires std::invocable<decltype(Callback), async&>
    status set_callback_static() noexcept {
      if (closing()) return status::from_native(UV_EBADF);
      static_dispatch_ = &invoke_static<Callback>;
      replace_callback({});
      return {};
    }

    // Cross-thread entry point. Producers must be stopped and joined (or
    // otherwise synchronized) before close. Reading closing() here would race
    // with loop-thread state without protecting the native storage lifetime.
    status send() noexcept {
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

    void init(uv_loop_t *loop) {
      throw_if_error(uv_async_init(loop, native(), &async::async_trampoline));
    }

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
