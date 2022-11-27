#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "stream.hpp"

namespace uv {

  struct Tty : TStream<uv_tty_t, Tty> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::Tty; }


    Tty(Loop *loop, File fd) {
      Error::safe(uv_tty_init(loop, this, fd, 0));
    }

    void setMode(uv_tty_mode_t mode) {
      Error::safe(uv_tty_set_mode(this, mode));
    }

    static void resetMode() {
      Error::safe(uv_tty_reset_mode());
    }

    void getWinsize(int &width, int &height) {
      Error::safe(uv_tty_get_winsize(this, &width, &height));
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 33
    static void setVtermState(uv_tty_vtermstate_t state) {
      uv_tty_set_vterm_state(state);
    }

    static void getVtermState(uv_tty_vtermstate_t &state) {
      Error::safe(uv_tty_get_vterm_state(&state));
    }
#endif

  };

}
