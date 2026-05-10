#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/stream.hpp"
#include "uvpp/requests/connect.hpp"

namespace uvpp {

  class pipe final : public stream<pipe, uv_pipe_t> {
  public:
    explicit pipe(loop &l, bool ipc = false) {
      throw_if_error(uv_pipe_init(l.native(), native(), ipc ? 1 : 0));
    }

    explicit pipe(loop_view l, bool ipc = false) {
      throw_if_error(uv_pipe_init(l.native(), native(), ipc ? 1 : 0));
    }

    void bind(std::string_view name) {
      std::string storage{name};
      throw_if_error(uv_pipe_bind(native(), storage.c_str()));
    }

    void connect(connect_request &request, std::string_view name, connect_request::callback callback) {
      request.set_callback(std::move(callback));
      std::string storage{name};
      uv_pipe_connect(request.native(), native(), storage.c_str(), &pipe::connect_trampoline);
    }

    template<auto Callback>
    void connect_static(connect_request &request, std::string_view name) {
      std::string storage{name};
      uv_pipe_connect(request.native(), native(), storage.c_str(), [](uv_connect_t *raw, int status) noexcept {
        detail::invoke_static_callback<Callback>(connect_request::from_native(raw), result{status});
      });
    }

    void open(uv_file file) {
      throw_if_error(uv_pipe_open(native(), file));
    }

    void pending_instances(int count) noexcept {
      uv_pipe_pending_instances(native(), count);
    }

    int pending_count() const noexcept {
      return uv_pipe_pending_count(const_cast<uv_pipe_t *>(native()));
    }

    uv_handle_type pending_type() const noexcept {
      return uv_pipe_pending_type(const_cast<uv_pipe_t *>(native()));
    }

  private:
    static void connect_trampoline(uv_connect_t *raw, int status) noexcept {
      connect_request::from_native(raw).invoke(status);
    }
  };

}
