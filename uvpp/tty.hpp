#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "stream.hpp"

namespace uv {

  struct Tty : TStream<uv_tty_t, Tty> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::Tty; }


    Tty(Loop *loop, uv_file fd) {
      _safe(uv_tty_init(loop, this, fd, 0));
    }

    void set_mode(uv_tty_mode_t mode) {
      _safe(uv_tty_set_mode(this, mode));
    }

    static void reset_mode() {
      _safe(uv_tty_reset_mode());
    }

    void get_winsize(int &width, int &height) {
      _safe(uv_tty_get_winsize(this, &width, &height));
    }

    static void set_vterm_state(uv_tty_vtermstate_t state) {
      uv_tty_set_vterm_state(state);
    }

    static void get_vterm_state(uv_tty_vtermstate_t &state) {
      _safe(uv_tty_get_vterm_state(&state));
    }
  };

}
