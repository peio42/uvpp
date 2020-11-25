#pragma once

#include "uv.h"
#include "error.hpp"

namespace uv {

  struct Loop;

  struct Thread {
    uv_thread_t tid;

    template<class Z>
    using Cb = void (*)(Z *arg);

    struct Options : uv_thread_options_t {
      Options() { flags = UV_THREAD_NO_FLAGS; }
      Options &set_stack_size(size_t _stack_size) {
        flags |= UV_THREAD_HAS_STACK_SIZE;
        stack_size = _stack_size;

        return *this;
      }
    };

    Thread(uv_thread_t _tid) : tid{_tid} { }

    template<class Z>
    Thread(Cb<Z> cb, Z *arg = nullptr) {
      _safe(uv_thread_create(&tid, reinterpret_cast<uv_thread_cb>(cb), static_cast<void *>(arg)));
    }

    Thread(Cb<void> cb) {
      _safe(uv_thread_create(&tid, reinterpret_cast<uv_thread_cb>(cb), nullptr));
    }

    template<class Z = void>
    Thread(Options *params, Cb<Z> cb, Z *arg = nullptr) {
      _safe(uv_thread_create_ex(&tid, params, reinterpret_cast<uv_thread_cb>(cb), static_cast<void *>(arg)));
    }

    static Thread self() {
      return Thread(uv_thread_self());
    }

    static void join(Thread &thread) {
      _safe(uv_thread_join(&thread.tid));
    }
  };

  inline bool operator==(const Thread &lhs, const Thread &rhs) {
    return uv_thread_equal(&lhs.tid, &rhs.tid);
  }


  struct Mutex {
    uv_mutex_t mx;

    Mutex(bool recursive = false) {
      if (recursive)
        _safe(uv_mutex_init_recursive(&mx));
      else
        _safe(uv_mutex_init(&mx));
    }

    ~Mutex() {
      uv_mutex_destroy(&mx);
    }

    void lock() {
      uv_mutex_lock(&mx);
    }

    bool trylock() {
      return _safe(uv_mutex_trylock(&mx));
    }

    void unlock() {
      uv_mutex_unlock(&mx);
    }
  };

}
