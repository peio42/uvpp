#pragma once

#include "uv.h"
#include "error.hpp"

namespace uv {

  struct Loop;
  struct Buffer;
  struct Handle;
  struct File;


  template<class T, class S>
  struct THandle : T {
    using AllocCb = void (*)(S *handle, size_t suggested_size, Buffer *buf);

    // uv_handle_t *as_handle() { return reinterpret_cast<uv_handle_t *>(this); }
    Handle *as_Handle() { return reinterpret_cast<Handle *>(this); }


    bool active() {
      return uv_is_active(as_Handle());
    }

    bool closing() {
      return uv_is_closing(as_Handle());
    }

    using CloseCb = void (*)(S *handle);
    void close(CloseCb cb = nullptr) {
      uv_close(as_Handle(), reinterpret_cast<uv_close_cb>(cb));
    }
    template<CloseCb cb>
    void close() {
      close(Error::safeCb<S, cb>);
    }

    void ref() {
      uv_ref(as_Handle());
    }

    void unref() {
      uv_unref(as_Handle());
    }

    bool has_ref() {
      return uv_has_ref(as_Handle());
    }

    void send_buffer_size(int &value) {
      Error::safe(uv_send_buffer_size(as_Handle(), &value));
    }

    void recv_buffer_size(int &value) {
      Error::safe(uv_recv_buffer_size(as_Handle(), &value));
    }

    void fileno(File &file) {
      return Error::safe(uv_fileno(as_Handle()), &file);
    }
    void fileno(int &fd) {
      Error::safe(uv_fileno(as_Handle(), &fd));
    }

    Loop *getLoop() {
      return static_cast<Loop *>(this->loop);
    }

    template<class V>
    void setData(V *data) { this->data = static_cast<void *>(data); }

    template<class V>
    V *getData() { return static_cast<V *>(this->data); }
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

    static inline const char *type_name(const Type type) {
      return uv_handle_type_name(static_cast<uv_handle_type>(type));
    }


    template<class Z>
    Z *cast() {
      if (! Z::castable(this))
        raise(Error(UV_EFTYPE));
      return reinterpret_cast<Z *>(this);
    }

    Type getType() {
      return static_cast<Type>(this->type);
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 19
    const char *getTypeName() {
      return uv_handle_type_name(this->type);
    }
#endif

 };

}
