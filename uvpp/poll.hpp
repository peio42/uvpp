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
      Disconnect = UV_DISCONNECT,
      Prioritized = UV_PRIORITIZED,
    };

    using PollCb = void (*)(Poll *handle, int status, int events);
    using SafePollCb = void (*)(Poll *handle, int events);

    bool castable(Handle &h) { return h.getType() == Handle::Type::Poll; }


    Poll(Loop *loop, int fd) {
      _safe(uv_poll_init(loop, this, fd));
    }

    // template<class V = void>
    // Poll(Loop *loop, int fd, V *data = nullptr) {
    //   uv_poll_init(loop, this, fd);
    //   this->data = static_cast<void *>(data);
    // }

    // Poll(Loop *loop, uv_os_sock_t sock) {
    //   uv_poll_init_socket(loop, this, sock);
    // }

    void start(Event events, PollCb cb) {
      _safe(uv_poll_start(this, static_cast<int>(events), reinterpret_cast<uv_poll_cb>(cb)));
    }
    template<SafePollCb cb>
    void start(Event events) {
      start(events, [](Poll *handle, int status, int events) {
          _safe(status);
          cb(handle, events);
        });
    }

    void stop() {
      _safe(uv_poll_stop(this));
    }
  };

  inline Poll::Event operator | (Poll::Event lhs, Poll::Event rhs) {
    return static_cast<Poll::Event>(static_cast<int>(lhs) | static_cast<int>(rhs));
  }
  inline Poll::Event operator & (Poll::Event lhs, Poll::Event rhs) {
    return static_cast<Poll::Event>(static_cast<int>(lhs) & static_cast<int>(rhs));
  }

}
