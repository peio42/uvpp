#pragma once

#include "uv.h"
#include "error.hpp"

namespace uv {

  struct Handle;


  struct Loop : uv_loop_t {
    enum class RunMode {
      Default = UV_RUN_DEFAULT,
      Once = UV_RUN_ONCE,
      NoWait = UV_RUN_NOWAIT,
    };


    Loop() {
      Error::safe(uv_loop_init(this));
    }

    enum class OptionLoopBlockSignalClass;
    static OptionLoopBlockSignalClass OptionLoopBlockSignal;
    void configure(const OptionLoopBlockSignalClass __attribute__((unused)) option, const int sig) {
      Error::safe(uv_loop_configure(this, UV_LOOP_BLOCK_SIGNAL, sig));
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 39
    enum class OptionMetricsIdleTimeClass;
    static OptionMetricsIdleTimeClass OptionMetricsIdleTime;
    void configure(const OptionMetricsIdleTimeClass __attribute__((unused)) option) {
      Error::safe(uv_loop_configure(this, UV_METRICS_IDLE_TIME));
    }
#endif

    void close() {
      Error::safe(uv_loop_close(this));
    }

    static Loop *getDefault() {
      return static_cast<Loop *>(uv_default_loop());
    }

    bool run(const RunMode mode = RunMode::Default) {
      return Error::safe(uv_run(this, static_cast<uv_run_mode>(mode)));
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

    template<class P>
    using WalkCb = void (*)(Handle *handle, P *arg);
    template<class P = void>
    void walk(const WalkCb<P> cb, P *arg = nullptr) {
      uv_walk(this, reinterpret_cast<uv_walk_cb>(cb), static_cast<void *>(arg));
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 12
    void fork() {
      Error::safe(uv_loop_fork(this));
    }
#endif

    template<class V = void>
    void setData(V *data) { this->data = static_cast<void *>(data); }

    template<class V>
    V *getData() { return static_cast<V *>(this->data); }
  };

}
