#pragma once

#include "uv.h"
#include "error.hpp"

namespace uv {

  struct Handle;


  struct Loop : uv_loop_t {
    template<class P>
    using WalkCb = void (*)(Handle *handle, P *arg);

    enum class RunMode {
      Default = UV_RUN_DEFAULT,
      Once = UV_RUN_ONCE,
      NoWait = UV_RUN_NOWAIT,
    };

    enum class OptionBlockSignalClass;
    static OptionBlockSignalClass OptionBlockSignal;


    Loop() {
      _safe(uv_loop_init(this));
    }

    void configure(const OptionBlockSignalClass __attribute__((unused)) option, const int sig) {
      _safe(uv_loop_configure(this, UV_LOOP_BLOCK_SIGNAL, sig));
    }

    void close() {
      _safe(uv_loop_close(this));
    }

    static Loop *getDefault() {
      return static_cast<Loop *>(uv_default_loop());
    }

    void run(const RunMode mode = RunMode::Default) {
      _safe(uv_run(this, static_cast<uv_run_mode>(mode)));
    }

    bool alive() {
      return uv_loop_alive(this);
    }

    void stop() {
      uv_stop(this);
    }

    int backend_fd() {
      return uv_backend_fd(this);
    }

    int backend_timeout() {
      return uv_backend_timeout(this);
    }

    uint64_t now() {
      return uv_now(this);
    }

    void update_time() {
      uv_update_time(this);
    }

    template<class P = void>
    void walk(const WalkCb<P> cb, P *arg = nullptr) {
      uv_walk(this, reinterpret_cast<uv_walk_cb>(cb), static_cast<void *>(arg));
    }

    void fork() {
      _safe(uv_loop_fork(this));
    }

    template<class V = void>
    void set(V *data) { this->data = static_cast<void *>(data); }

    template<class V>
    V *get() { return static_cast<V *>(this->data); }
  };

}
