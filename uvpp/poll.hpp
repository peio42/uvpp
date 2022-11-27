#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Poll : THandle<uv_poll_t, Poll> {
    enum class Event : int {
      Readable = UV_READABLE,
      Writable = UV_WRITABLE,
#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 9
      Disconnect = UV_DISCONNECT,
#endif
#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 14
      Prioritized = UV_PRIORITIZED,
#endif
    };

    bool castable(Handle &h) { return h.getType() == Handle::Type::Poll; }


    Poll(Loop *loop, int fd) {
      Error::safe(uv_poll_init(loop, this, fd));
    }
//    Poll(Loop *loop, uv_os_sock_t sock) {
//      Error::safe(uv_poll_init_socket(loop, this, sock));
//    }

    // template<class V = void>
    // Poll(Loop *loop, int fd, V *data = nullptr) {
    //   uv_poll_init(loop, this, fd);
    //   this->data = static_cast<void *>(data);
    // }

    // Poll(Loop *loop, uv_os_sock_t sock) {
    //   Error::safe(uv_poll_init_socket(loop, this, sock));
    // }

    using StartCb = void (*)(Poll *handle, int status, int events);
    using SafeStartCb = void (*)(Poll *handle, int events);
    void start(Event events, StartCb cb) {
      Error::safe(uv_poll_start(this, static_cast<int>(events), reinterpret_cast<uv_poll_cb>(cb)));
    }
    template<SafeStartCb cb>
    void start(Event events) {
      start(events, [](Poll *handle, int status, int events) {
          Error::safe(status);
          cb(handle, events);
        });
    }

    void stop() {
      Error::safe(uv_poll_stop(this));
    }
  };

  inline Poll::Event operator | (Poll::Event lhs, Poll::Event rhs) {
    return static_cast<Poll::Event>(static_cast<int>(lhs) | static_cast<int>(rhs));
  }
  inline Poll::Event operator & (Poll::Event lhs, Poll::Event rhs) {
    return static_cast<Poll::Event>(static_cast<int>(lhs) & static_cast<int>(rhs));
  }

}
