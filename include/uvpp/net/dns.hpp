#pragma once

#include <concepts>
#include <coroutine>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/co/task.hpp"
#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/native.hpp"
#include "uvpp/net/address.hpp"
#include "uvpp/net/socket_address.hpp"
#include "uvpp/requests/dns.hpp"

namespace uv {

  // The high-level DNS value owns copies of every address returned by libuv.
  // It is therefore independent of the request and can outlive the awaiting
  // coroutine frame.
  using resolved_addresses = std::vector<address_info>;
  using resolve_result = result<resolved_addresses>;

  namespace detail {

    inline void getaddrinfo_trampoline(uv_getaddrinfo_t *raw, int status, addrinfo *addresses) noexcept {
      getaddrinfo_request::from_native(raw).invoke(status, addresses);
    }

    inline void getnameinfo_trampoline(uv_getnameinfo_t *raw, int status, const char *hostname,
                                       const char *service) noexcept {
      getnameinfo_request::from_native(raw).invoke(status, hostname, service);
    }

    inline void submit_getaddrinfo(loop_view loop, getaddrinfo_request &request,
                                   std::optional<std::string_view> node,
                                   std::optional<std::string_view> service,
                                   const addrinfo *hints,
                                   getaddrinfo_request::callback callback) {
      request.set_inputs(node, service, hints);
      detail::submit_request(request, std::move(callback),
        [&] {
          return uv_getaddrinfo(loop.native(), request.native(), getaddrinfo_trampoline,
                                request.node_arg(), request.service_arg(), request.hints_arg());
        },
        [&] { request.clear_inputs(); });
    }

    template<auto Callback>
    void submit_getaddrinfo_static(loop_view loop, getaddrinfo_request &request,
                                   std::optional<std::string_view> node,
                                   std::optional<std::string_view> service,
                                   const addrinfo *hints) {
      request.set_inputs(node, service, hints);
      detail::submit_with_rollback(
        [&] {
          return uv_getaddrinfo(loop.native(), request.native(),
            [](uv_getaddrinfo_t *raw, int status, addrinfo *addresses) noexcept {
              getaddrinfo_request::from_native(raw).template invoke_static<Callback>(status, addresses);
            },
            request.node_arg(), request.service_arg(), request.hints_arg());
        },
        [&] { request.clear_inputs(); });
    }

    inline void submit_getnameinfo(loop_view loop, getnameinfo_request &request, const sockaddr *addr,
                                   int flags, getnameinfo_request::callback callback) {
      request.set_address(addr);
      detail::submit_request(request, std::move(callback),
        [&] {
          return uv_getnameinfo(loop.native(), request.native(), getnameinfo_trampoline,
                                request.address_arg(), flags);
        },
        [&] { request.clear_address(); });
    }

    template<auto Callback>
    void submit_getnameinfo_static(loop_view loop, getnameinfo_request &request, const sockaddr *addr,
                                   int flags) {
      request.set_address(addr);
      detail::submit_with_rollback(
        [&] {
          return uv_getnameinfo(loop.native(), request.native(),
            [](uv_getnameinfo_t *raw, int status, const char *hostname, const char *service) noexcept {
              getnameinfo_request::from_native(raw).template invoke_static<Callback>(status, hostname, service);
            },
            request.address_arg(), flags);
        },
        [&] { request.clear_address(); });
    }

    // One resolver request lives in its awaiting coroutine frame.  native_storage
    // makes the uv_getaddrinfo_t address stable without using uv_req_t::data,
    // which remains available to applications using the low-level wrapper.
    template<bool ExplicitResult>
    class resolve_awaiter final
      : private native_storage<resolve_awaiter<ExplicitResult>, uv_getaddrinfo_t, uv_req_t> {
      using storage_type = native_storage<resolve_awaiter<ExplicitResult>, uv_getaddrinfo_t, uv_req_t>;

    public:
      resolve_awaiter(std::optional<std::string_view> node,
                      std::optional<std::string_view> service,
                      const addrinfo *hints)
        : node_{node ? std::optional<std::string>{std::string{*node}} : std::nullopt},
          service_{service ? std::optional<std::string>{std::string{*service}} : std::nullopt} {
        if (hints != nullptr) {
          // getaddrinfo only consults these scalar hint fields.  Do not retain
          // caller pointers such as ai_addr, ai_canonname, or ai_next.
          hints_.emplace();
          hints_->ai_flags = hints->ai_flags;
          hints_->ai_family = hints->ai_family;
          hints_->ai_socktype = hints->ai_socktype;
          hints_->ai_protocol = hints->ai_protocol;
        }
      }

      resolve_awaiter(const resolve_awaiter &) = delete;
      resolve_awaiter &operator=(const resolve_awaiter &) = delete;
      resolve_awaiter(resolve_awaiter &&) = delete;
      resolve_awaiter &operator=(resolve_awaiter &&) = delete;

      bool await_ready() const noexcept { return false; }

      template<class Promise>
        requires std::derived_from<Promise, co::detail::task_promise_base>
      bool await_suspend(std::coroutine_handle<Promise> continuation) {
        // The resolver deliberately takes no loop argument: a cold task acquires
        // its one loop at spawn and this request always submits on that loop.
        auto &promise = continuation.promise();
        if (promise.stop_requested()) {
          status_ = UV_ECANCELED;
          return false;
        }

        continuation_ = continuation;
        status_ = uv_getaddrinfo(promise.execution_loop().native(), this->native(),
            &resolve_awaiter::on_complete, node_arg(), service_arg(), hints_arg());
        if (status_ < 0) {
          continuation_ = {};
          clear_inputs();
          return false;
        }

        submitted_ = true;
        cancellation_ = promise.cancellation();
        if (cancellation_ != nullptr && !cancellation_->register_callback(
            cancellation_registration_, &resolve_awaiter::on_stop_requested, this)) {
          request_native_cancellation();
        }
        return true;
      }

      auto await_resume() {
        if constexpr (ExplicitResult) {
          if (status_ < 0) {
            return resolve_result{make_error_code(status_)};
          }
          return resolve_result{completion_.to_vector()};
        } else {
          throw_if_error(status_);
          return completion_.to_vector();
        }
      }

    private:
      static void on_complete(uv_getaddrinfo_t *raw, int status, addrinfo *addresses) noexcept {
        // The native object is the first object in storage_type.  Keep this
        // recovery private so this high-level request exposes no raw request
        // ownership API and never repurposes uv_req_t::data.
        auto *storage = reinterpret_cast<storage_type *>(raw);
        auto &self = *static_cast<resolve_awaiter *>(storage);
        self.complete(status, addresses);
      }

      static void on_stop_requested(void *context) noexcept {
        static_cast<resolve_awaiter *>(context)->request_native_cancellation();
      }

      void request_native_cancellation() noexcept {
        if (!submitted_) {
          return;
        }
        // Cancellation is only a request.  UV_EBUSY (or a racing successful
        // completion) leaves the awaiter and its native request intact until
        // on_complete performs terminal delivery.
        (void)uv_cancel(reinterpret_cast<uv_req_t *>(this->native()));
      }

      void complete(int status, addrinfo *addresses) noexcept {
        release_cancellation();
        submitted_ = false;
        status_ = status;
        completion_ = getaddrinfo_result{status, addresses};
        clear_inputs();

        auto continuation = std::exchange(continuation_, {});
        continuation.resume();
      }

      void release_cancellation() noexcept {
        if (cancellation_ != nullptr) {
          cancellation_->unregister(cancellation_registration_);
          cancellation_ = nullptr;
        }
      }

      const char *node_arg() const noexcept { return node_ ? node_->c_str() : nullptr; }
      const char *service_arg() const noexcept { return service_ ? service_->c_str() : nullptr; }
      const addrinfo *hints_arg() const noexcept { return hints_ ? &*hints_ : nullptr; }

      void clear_inputs() noexcept {
        node_.reset();
        service_.reset();
        hints_.reset();
      }

      std::optional<std::string> node_;
      std::optional<std::string> service_;
      std::optional<addrinfo> hints_;
      getaddrinfo_result completion_{};
      std::coroutine_handle<> continuation_{};
      co::detail::cancellation_state *cancellation_ = nullptr;
      co::detail::cancellation_registration cancellation_registration_{};
      int status_ = 0;
      bool submitted_ = false;
    };

  }

  // Resolve a node/service pair on the awaiting task's loop.  Inputs and the
  // relevant scalar hint fields are copied before submission.  Operational
  // submission and completion failures throw uv::error at co_await; allocation
  // while copying inputs or materializing the returned vector may still throw.
  [[nodiscard]] inline detail::resolve_awaiter<false> resolve(
      std::string_view node, std::string_view service, const addrinfo *hints = nullptr) {
    return detail::resolve_awaiter<false>{std::optional<std::string_view>{node},
                                          std::optional<std::string_view>{service}, hints};
  }

  [[nodiscard]] inline detail::resolve_awaiter<false> resolve(
      std::string_view node, const addrinfo *hints = nullptr) {
    return detail::resolve_awaiter<false>{std::optional<std::string_view>{node}, std::nullopt, hints};
  }

  [[nodiscard]] inline detail::resolve_awaiter<false> resolve(
      std::nullptr_t, std::string_view service, const addrinfo *hints = nullptr) {
    return detail::resolve_awaiter<false>{std::nullopt, std::optional<std::string_view>{service}, hints};
  }

  namespace ops {

    // Explicit-result counterpart of uv::resolve.  Native submission and
    // completion failures, including completed cancellation, are returned as
    // resolve_result errors.  C++ setup/materialization failures may throw.
    [[nodiscard]] inline detail::resolve_awaiter<true> resolve(
        std::string_view node, std::string_view service, const addrinfo *hints = nullptr) {
      return detail::resolve_awaiter<true>{std::optional<std::string_view>{node},
                                           std::optional<std::string_view>{service}, hints};
    }

    [[nodiscard]] inline detail::resolve_awaiter<true> resolve(
        std::string_view node, const addrinfo *hints = nullptr) {
      return detail::resolve_awaiter<true>{std::optional<std::string_view>{node}, std::nullopt, hints};
    }

    [[nodiscard]] inline detail::resolve_awaiter<true> resolve(
        std::nullptr_t, std::string_view service, const addrinfo *hints = nullptr) {
      return detail::resolve_awaiter<true>{std::nullopt, std::optional<std::string_view>{service}, hints};
    }

  } // namespace ops

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    detail::submit_getaddrinfo(loop, request, std::optional<std::string_view>{node},
                               std::optional<std::string_view>{service}, hints, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, node, service, hints, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop, request, node, service, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, node, service, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    detail::submit_getaddrinfo(loop, request, std::nullopt,
                               std::optional<std::string_view>{service}, hints, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, nullptr, service, hints, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop, request, nullptr, service, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, nullptr, service, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    detail::submit_getaddrinfo(loop, request, std::optional<std::string_view>{node},
                               std::nullopt, hints, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          const addrinfo *hints,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, node, nullptr, hints, std::move(callback));
  }

  inline void getaddrinfo(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop, request, node, nullptr, nullptr, std::move(callback));
  }

  inline void getaddrinfo(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          getaddrinfo_request::callback callback) {
    getaddrinfo(loop.view(), request, node, nullptr, nullptr, std::move(callback));
  }

  template<auto Callback>
  void getaddrinfo_static(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          const addrinfo *hints = nullptr) {
    detail::submit_getaddrinfo_static<Callback>(loop, request, std::optional<std::string_view>{node},
                                                std::optional<std::string_view>{service}, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::string_view service,
                          const addrinfo *hints = nullptr) {
    getaddrinfo_static<Callback>(loop.view(), request, node, service, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop_view loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          const addrinfo *hints = nullptr) {
    detail::submit_getaddrinfo_static<Callback>(loop, request, std::nullopt,
                                                std::optional<std::string_view>{service}, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop &loop, getaddrinfo_request &request,
                          std::nullptr_t, std::string_view service,
                          const addrinfo *hints = nullptr) {
    getaddrinfo_static<Callback>(loop.view(), request, nullptr, service, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop_view loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          const addrinfo *hints = nullptr) {
    detail::submit_getaddrinfo_static<Callback>(loop, request, std::optional<std::string_view>{node},
                                                std::nullopt, hints);
  }

  template<auto Callback>
  void getaddrinfo_static(loop &loop, getaddrinfo_request &request,
                          std::string_view node, std::nullptr_t,
                          const addrinfo *hints = nullptr) {
    getaddrinfo_static<Callback>(loop.view(), request, node, nullptr, hints);
  }

  inline void getnameinfo(loop_view loop, getnameinfo_request &request, const sockaddr *addr,
                          int flags, getnameinfo_request::callback callback) {
    detail::submit_getnameinfo(loop, request, addr, flags, std::move(callback));
  }

  inline void getnameinfo(loop &loop, getnameinfo_request &request, const sockaddr *addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop.view(), request, addr, flags, std::move(callback));
  }

  inline void getnameinfo(loop_view loop, getnameinfo_request &request, const ipv4 &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop, request, addr.native_sockaddr(), flags, std::move(callback));
  }

  inline void getnameinfo(loop &loop, getnameinfo_request &request, const ipv4 &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop.view(), request, addr, flags, std::move(callback));
  }

  inline void getnameinfo(loop_view loop, getnameinfo_request &request, const ipv6 &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop, request, addr.native_sockaddr(), flags, std::move(callback));
  }

  inline void getnameinfo(loop &loop, getnameinfo_request &request, const ipv6 &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop.view(), request, addr, flags, std::move(callback));
  }

  inline void getnameinfo(loop_view loop, getnameinfo_request &request, const socket_address &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop, request, addr.native(), flags, std::move(callback));
  }

  inline void getnameinfo(loop &loop, getnameinfo_request &request, const socket_address &addr,
                          int flags, getnameinfo_request::callback callback) {
    getnameinfo(loop.view(), request, addr, flags, std::move(callback));
  }

  template<auto Callback>
  void getnameinfo_static(loop_view loop, getnameinfo_request &request, const sockaddr *addr,
                          int flags) {
    detail::submit_getnameinfo_static<Callback>(loop, request, addr, flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop &loop, getnameinfo_request &request, const sockaddr *addr,
                          int flags) {
    getnameinfo_static<Callback>(loop.view(), request, addr, flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop_view loop, getnameinfo_request &request, const ipv4 &addr,
                          int flags) {
    getnameinfo_static<Callback>(loop, request, addr.native_sockaddr(), flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop &loop, getnameinfo_request &request, const ipv4 &addr,
                          int flags) {
    getnameinfo_static<Callback>(loop.view(), request, addr, flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop_view loop, getnameinfo_request &request, const ipv6 &addr,
                          int flags) {
    getnameinfo_static<Callback>(loop, request, addr.native_sockaddr(), flags);
  }

  template<auto Callback>
  void getnameinfo_static(loop &loop, getnameinfo_request &request, const ipv6 &addr,
                          int flags) {
    getnameinfo_static<Callback>(loop.view(), request, addr, flags);
  }

}
