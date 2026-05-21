#pragma once

#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/handles/stream.hpp"

namespace uv {

  enum class tty_mode {
    normal = UV_TTY_MODE_NORMAL,
    raw = UV_TTY_MODE_RAW,
    io = UV_TTY_MODE_IO
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

  class tty final : public stream<tty, uv_tty_t> {
  public:
    tty(loop &l, uv_file file, bool readable) {
      throw_if_error(uv_tty_init(l.native(), native(), file, readable ? 1 : 0));
    }

    tty(loop_view l, uv_file file, bool readable) {
      throw_if_error(uv_tty_init(l.native(), native(), file, readable ? 1 : 0));
    }

    void set_mode(tty_mode mode) {
      throw_if_error(uv_tty_set_mode(native(), static_cast<uv_tty_mode_t>(mode)));
    }

    terminal_size size() const {
      terminal_size out;
      throw_if_error(uv_tty_get_winsize(const_cast<uv_tty_t *>(native()), &out.width, &out.height));
      return out;
    }

    static void reset_mode() {
      throw_if_error(uv_tty_reset_mode());
    }

#if UVPP_HAS_TTY_VTERM_STATE
    static void set_vterm_state(tty_vterm_state state) noexcept {
      uv_tty_set_vterm_state(static_cast<uv_tty_vtermstate_t>(state));
    }

    static tty_vterm_state vterm_state() {
      uv_tty_vtermstate_t state{};
      throw_if_error(uv_tty_get_vterm_state(&state));
      return static_cast<tty_vterm_state>(state);
    }
#endif
  };

}
