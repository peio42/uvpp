#pragma once

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/detail/initializable_handle.hpp"
#include "uvpp/handles/stream.hpp"

namespace uv {

  enum class tty_mode {
    normal = UV_TTY_MODE_NORMAL,
    raw = UV_TTY_MODE_RAW,
    io = UV_TTY_MODE_IO
#if UVPP_HAS_TTY_RAW_VT
    , raw_vt = UV_TTY_MODE_RAW_VT
#endif
  };

#if UVPP_HAS_TTY_VTERM_STATE
  enum class tty_vterm_state {
    supported = UV_TTY_SUPPORTED,
    unsupported = UV_TTY_UNSUPPORTED
  };
#endif

  struct terminal_size {
    int width = 0;
    int height = 0;
  };

}

namespace uv::raw {

  // These operations affect libuv's process-wide terminal state and do not
  // require an initialized handle.
  inline status tty_reset_mode() noexcept {
    return status::from_native(uv_tty_reset_mode());
  }

#if UVPP_HAS_TTY_VTERM_STATE
  inline void tty_set_vterm_state(tty_vterm_state state) noexcept {
    uv_tty_set_vterm_state(static_cast<uv_tty_vtermstate_t>(state));
  }

  inline result<tty_vterm_state> tty_get_vterm_state() noexcept {
    uv_tty_vtermstate_t state{};
    const auto code = uv_tty_get_vterm_state(&state);
    if (code < 0) return result<tty_vterm_state>{make_error_code(code)};
    return result<tty_vterm_state>{static_cast<tty_vterm_state>(state)};
  }
#endif

  class tty final : public detail::initializable_handle<tty, uv_tty_t, stream<tty, uv_tty_t>> {
  public:
    tty() noexcept = default;
    tty(loop &l, uv_file file, bool readable) : tty(l.view(), file, readable) {}
    tty(loop_view l, uv_file file, bool readable) { init(l, file, readable).throw_if_error(); }

    status init(loop &l, uv_file file, bool readable) noexcept { return init(l.view(), file, readable); }
    status init(loop_view l, uv_file file, bool readable) noexcept {
      if (initialized()) return status::from_native(UV_EALREADY);
      auto *data = native()->data;
      const int code = uv_tty_init(l.native(), native(), file, readable ? 1 : 0);
      if (code < 0) {
        // Reviewed Unix/Windows libuv paths either fail before registration or
        // remove their own late registration. Do not unlink or close again.
        *native() = {};
        native()->data = data;
      }
      return status::from_native(code);
    }

    status set_mode(tty_mode mode) noexcept {
      if (!initialized() || closing()) return status::from_native(UV_EBADF);
      switch (mode) {
        case tty_mode::normal:
        case tty_mode::raw:
        case tty_mode::io:
#if UVPP_HAS_TTY_RAW_VT
        case tty_mode::raw_vt:
#endif
          break;
        default:
          return status::from_native(UV_EINVAL);
      }
      return status::from_native(uv_tty_set_mode(native(), static_cast<uv_tty_mode_t>(mode)));
    }

    result<terminal_size> size() const noexcept {
      if (!initialized() || closing()) return result<terminal_size>{make_error_code(UV_EBADF)};
      terminal_size out;
      const int code = uv_tty_get_winsize(const_cast<uv_tty_t *>(native()), &out.width, &out.height);
      if (code < 0) return result<terminal_size>{make_error_code(code)};
      return result<terminal_size>{out};
    }
  };

}
