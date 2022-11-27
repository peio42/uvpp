#pragma once

#include "uv.h"
#include "error.hpp"
#include "fs.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "stream.hpp"

namespace uv {

  struct Pipe : TStream<uv_pipe_t, Pipe> {
    bool castable(Handle &h) { return h.getType() == Handle::Type::NamedPipe; }


    Pipe() = default;

    Pipe(Loop *loop, bool ipc = false) {
      Error::safe(uv_pipe_init(loop, this, ipc));
    }

    void init(Loop *loop, bool ipc = false) {
      Error::safe(uv_pipe_init(loop, this, ipc));
    }

    void open(File file) {
      Error::safe(uv_pipe_open(this, file));
    }

    void bind(const char *name) {
      Error::safe(uv_pipe_bind(this, name));
    }

    void connect(ConnectRq *req, const char *name, ConnectCb cb) {
      uv_pipe_connect(req, this, name, reinterpret_cast<uv_connect_cb>(cb));
    }
    template<SafeConnectCb cb>
    void connect(ConnectRq *req, const char *name) {
      connect(req, name, Error::safeCb<ConnectRq, cb>);
    }


    void getsockname(char *buffer, size_t *size) {
      Error::safe(uv_pipe_getsockname(this, buffer, size));
    }

    void getpeername(char *buffer, size_t *size) {
      Error::safe(uv_pipe_getpeername(this, buffer, size));
    }

    void pending_instances(int count) {
      uv_pipe_pending_instances(this, count);
    }

    int pending_count() {
      return uv_pipe_pending_count(this);
    }

    Handle::Type pending_type() {
      return static_cast<Handle::Type>(uv_pipe_pending_type(this));
    }

    void chmod(int flags) {
      Error::safe(uv_pipe_chmod(this, flags));
    }

    static void pipe(File fds[2], int read_flags, int write_flags) {
      Error::safe(uv_pipe(reinterpret_cast<uv_file *>(fds), read_flags, write_flags));
    }

  };

}
