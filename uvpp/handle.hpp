#pragma once

#include "uv.h"
#include "error.hpp"

namespace uv {

  struct Loop;
  struct Buffer;
  struct Handle;


  template<class T, class S>
  struct THandle : T {
    using AllocCb = void (*)(S *handle, size_t suggested_size, Buffer *buf);
    using CloseCb = void (*)(S *handle);

    // uv_handle_t *as_handle() { return reinterpret_cast<uv_handle_t *>(this); }
    Handle *as_handle() { return reinterpret_cast<Handle *>(this); }


    bool active() {
      return uv_is_active(as_handle());
    }

    bool closing() {
      return uv_is_closing(as_handle());
    }

    void close(CloseCb cb = nullptr) {
      uv_close(as_handle(), reinterpret_cast<uv_close_cb>(cb));
    }

    void ref() {
      uv_ref(as_handle());
    }

    void unref() {
      uv_unref(as_handle());
    }

    bool has_ref() {
      return uv_has_ref(as_handle());
    }

    void send_buffer_size(int *value) {
      _safe(uv_send_buffer_size(as_handle(), value));
    }

    void recv_buffer_size(int *value) {
      _safe(uv_recv_buffer_size(as_handle(), value));
    }

    void fileno(int *fd) {
      _safe(uv_fileno(as_handle(), fd));
    }

    Loop *getLoop() {
      return static_cast<Loop *>(this->loop);
    }

    template<class V>
    void set(V *data) { this->data = static_cast<void *>(data); }

    template<class V>
    V *get() { return static_cast<V *>(this->data); }
  };


  struct Handle : THandle<uv_handle_t, Handle> {
    enum class Type {
      Unknown = UV_UNKNOWN_HANDLE,
      Async = UV_ASYNC,
      Check = UV_CHECK,
      FsEvent = UV_FS_EVENT,
      FsPoll = UV_FS_POLL,
      Handle = UV_HANDLE,
      Idle = UV_IDLE,
      NamedPipe = UV_NAMED_PIPE,
      Poll = UV_POLL,
      Prepare = UV_PREPARE,
      Process = UV_PROCESS,
      Stream = UV_STREAM,
      Tcp = UV_TCP,
      Timer = UV_TIMER,
      Tty = UV_TTY,
      Udp = UV_UDP,
      Signal = UV_SIGNAL,
      File = UV_FILE,
      Max = UV_HANDLE_TYPE_MAX
    };


    template<class Z>
    Z *cast() {
      if (! Z::castable(this))
        raise(1);
      return reinterpret_cast<Z *>(this);
    }

    Type getType() {
      return static_cast<Type>(this->type);
    }
 };

}
