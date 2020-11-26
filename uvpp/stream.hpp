#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "request.hpp"

namespace uv {

  struct Buffer;
  struct Stream;

  template<class T, class S>
  struct TStream : THandle<T, S> {
    using ConnectRq = Request<uv_connect_t, S>;
    using ShutdownRq = Request<uv_shutdown_t, S>;
    struct WriteRq : Request<uv_write_t, S> {
      S *send_handle() { return reinterpret_cast<S *>(this->send_handle); }
    };

    using AllocCb = typename THandle<T, S>::AllocCb;
    using ReadCb = void (*)(S *stream, ssize_t nread, const Buffer *buf);
    using WriteCb = void (*)(WriteRq *req, int status);
    using ConnectCb = void (*)(ConnectRq *req, int status);
    using ShutdownCb = void (*)(ShutdownRq *req, int status);
    using ConnectionCb = void (*)(S *server, int status);

    using SafeReadCb = void (*)(S *stream, ssize_t nread, const Buffer *buf);
    using SafeEofCb = void (*)(S *server, const Buffer *buf);
    using SafeWriteCb = void (*)(WriteRq *req);
    using SafeConnectCb = void (*)(ConnectRq *req);
    using SafeShutdownCb = void (*)(ShutdownRq *req);
    using SafeConnectionCb = void (*)(S *server);

    const static int DEFAULT_BACKLOG = 64;


    // uv_stream_t *as_stream() { return reinterpret_cast<uv_stream_t *>(this); }
    Stream *as_stream() { return reinterpret_cast<Stream *>(this); }


    void shutdown(ShutdownRq *req, ShutdownCb cb) {
      _safe(uv_shutdown(req, as_stream(), reinterpret_cast<uv_shutdown_cb>(cb)));
    }
    template<SafeShutdownCb cb>
    void shutdown(ShutdownRq *req) {
      shutdown(req, _safeCb<req, cb>);
    }

    void listen(ConnectionCb cb, int backlog = DEFAULT_BACKLOG) {
      _safe(uv_listen(as_stream(), backlog, reinterpret_cast<uv_connection_cb>(cb)));
    }
    template<SafeConnectionCb cb>
    void listen(int backlog = DEFAULT_BACKLOG) {
      listen(_safeCb<S, cb>, backlog);
    }

    template<class Z>
    void accept(Z *client) {
      _safe(uv_accept(as_stream(), client->as_stream()));
    }

    void read_start(AllocCb alloc_cb, ReadCb read_cb) {
      _safe(uv_read_start(as_stream(), reinterpret_cast<uv_alloc_cb>(alloc_cb), reinterpret_cast<uv_read_cb>(read_cb)));
    }
    template<AllocCb acb, SafeReadCb rcb, SafeEofCb ecb>
    void read_start() {
      read_start(acb, [](S *stream, ssize_t nread, const Buffer *buf) {
          if (nread < 0) {
            if (nread == UV_EOF)
              ecb(stream, buf);
            else
              _safe(nread);
          } else
            rcb(stream, nread, buf);
        });
    }

    void read_stop() {
      _safe(uv_read_stop(as_stream()));
    }

    void write(WriteRq *req, const Buffer bufs[], unsigned int nbufs, WriteCb cb) {
      _safe(uv_write(req, as_stream(), bufs, nbufs, reinterpret_cast<uv_write_cb>(cb)));
    }
    template<SafeWriteCb cb>
    void write(WriteRq *req, const Buffer bufs[], unsigned int nbufs) {
      write(req, bufs, nbufs, _safeCb<WriteRq, cb>);
    }

    template<class Z>
    void write(WriteRq *req, const Buffer bufs[], unsigned int nbufs, Z *send_handle, WriteCb cb) {
      _safe(uv_write2(req, as_stream(), bufs, nbufs, send_handle->as_stream(), _safeCb<WriteRq, cb>));
    }
    template<SafeWriteCb cb, class Z>
    void write(WriteRq *req, const Buffer bufs[], unsigned int nbufs, Z *send_handle) {
      write(req, bufs, nbufs, send_handle, _safeCb<req, cb>);
    }

    int try_write(const Buffer bufs[], unsigned int nbufs) {
      return _safe(uv_try_write(as_stream(), bufs, nbufs));
    }

    bool readable() {
      return uv_is_readable(as_stream());
    }

    bool writable() {
      return uv_is_writable(as_stream());
    }

    void blocking(bool bl) {
      _safe(uv_stream_set_blocking(as_stream(), bl));
    }
  };


  struct Stream : TStream<uv_stream_t, Stream> {
    bool castable(Handle &h) {
      return
        h.getType() == Handle::Type::Tcp ||
        h.getType() == Handle::Type::NamedPipe ||
        h.getType() == Handle::Type::Tty;
    }
  };


}
