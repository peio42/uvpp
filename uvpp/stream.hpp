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
    using ConnectCb = void (*)(ConnectRq *req, int status);
    using SafeConnectCb = void (*)(ConnectRq *req);

    const static int DEFAULT_BACKLOG = 64;


    Stream *as_Stream() { return reinterpret_cast<Stream *>(this); }


    using ShutdownRq = Request<uv_shutdown_t, S>;
    using ShutdownCb = void (*)(ShutdownRq *req, int status);
    using SafeShutdownCb = void (*)(ShutdownRq *req);
    void shutdown(ShutdownRq *req, ShutdownCb cb) {
      Error::safe(uv_shutdown(req, as_Stream(), reinterpret_cast<uv_shutdown_cb>(cb)));
    }
    template<SafeShutdownCb cb>
    void shutdown(ShutdownRq *req) {
      shutdown(req, Error::safeCb<req, cb>);
    }

    using ConnectionCb = void (*)(S *server, int status);
    using SafeConnectionCb = void (*)(S *server);
    void listen(ConnectionCb cb, int backlog = DEFAULT_BACKLOG) {
      Error::safe(uv_listen(as_Stream(), backlog, reinterpret_cast<uv_connection_cb>(cb)));
    }
    template<SafeConnectionCb cb>
    void listen(int backlog = DEFAULT_BACKLOG) {
      listen(Error::safeCb<S, cb>, backlog);
    }

    template<class Z>
    void accept(Z *client) {
      Error::safe(uv_accept(as_Stream(), client->as_Stream()));
    }

    using AllocCb = typename THandle<T, S>::AllocCb;
    using ReadCb = void (*)(S *stream, ssize_t nread, const Buffer *buf);
    using SafeReadCb = void (*)(S *stream, ssize_t nread, const Buffer *buf);
    using SafeEofCb = void (*)(S *server, const Buffer *buf);
    void read_start(AllocCb alloc_cb, ReadCb read_cb) {
      Error::safe(uv_read_start(as_Stream(), reinterpret_cast<uv_alloc_cb>(alloc_cb), reinterpret_cast<uv_read_cb>(read_cb)));
    }
    template<AllocCb acb, SafeReadCb rcb, SafeEofCb ecb>
    void read_start() {
      read_start(acb, [](S *stream, ssize_t nread, const Buffer *buf) {
          if (nread < 0) {
            if (nread == UV_EOF)
              ecb(stream, buf);
            else
              Error::safe(nread);
          } else
            rcb(stream, nread, buf);
        });
    }

    bool read_stop() {
      return uv_read_stop(as_Stream());
    }

    struct WriteRq : Request<uv_write_t, S> {
      S *sendHandle() { return reinterpret_cast<S *>(this->send_handle); }
    };
    using WriteCb = void (*)(WriteRq *req, int status);
    using SafeWriteCb = void (*)(WriteRq *req);
    void write(WriteRq *req, const Buffer bufs[], unsigned int nbufs, WriteCb cb) {
      Error::safe(uv_write(req, as_Stream(), bufs, nbufs, reinterpret_cast<uv_write_cb>(cb)));
    }
    template<SafeWriteCb cb>
    void write(WriteRq *req, const Buffer bufs[], unsigned int nbufs) {
      write(req, bufs, nbufs, Error::safeCb<WriteRq, cb>);
    }

    template<class Z>
    void write(WriteRq *req, const Buffer bufs[], unsigned int nbufs, Z *sendHandle, WriteCb cb) {
      Error::safe(uv_write2(req, as_Stream(), bufs, nbufs, sendHandle->as_stream(), Error::safeCb<WriteRq, cb>));
    }
    template<SafeWriteCb cb, class Z>
    void write(WriteRq *req, const Buffer bufs[], unsigned int nbufs, Z *sendHandle) {
      write(req, bufs, nbufs, sendHandle, Error::safeCb<req, cb>);
    }

    int try_write(const Buffer bufs[], unsigned int nbufs) {
      return Error::safe(uv_try_write(as_Stream(), bufs, nbufs));
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 42
    template<class Z>
    int try_write(const Buffer bufs[], unsigned int nbufs, Z *sendHandle) {
      return Error::safe(uv_try_write2(as_Stream(), bufs, nbufs, sendHandle->as_stream()));
    }
#endif

    bool readable() {
      return uv_is_readable(as_Stream());
    }

    bool writable() {
      return uv_is_writable(as_Stream());
    }

    void blocking(bool bl) {
      Error::safe(uv_stream_set_blocking(as_Stream(), bl));
    }

    bool blocking() {
      return uv_stream_get_blocking(as_Stream());
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 19
    size_t write_queue_size() {
      return uv_stream_get_write_queue_size(as_Stream());
    }
#endif

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
