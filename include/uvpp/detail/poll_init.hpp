#pragma once

#include <cassert>

#include <uv.h>

namespace uv::detail {

  // libuv 1.x has used both a pointer array and a named queue structure here.
  // This is deliberately confined to rollback of an uncommitted poll init.
  template<class Queue>
  void unlink_poll_init_registration(Queue &queue) noexcept {
    if constexpr (requires { queue.next; queue.prev; }) {
      queue.next->prev = queue.prev;
      queue.prev->next = queue.next;
    } else {
      auto *next = static_cast<void **>(queue[0]);
      auto *prev = static_cast<void **>(queue[1]);
      next[1] = queue[1];
      prev[0] = queue[0];
    }
  }

  // Windows may fail SO_PROTOCOL_INFOW after uv__handle_init has registered
  // the inactive handle, before any poll requests or handle-owned resources
  // exist. Merely clearing loop would leave a dangling loop registration;
  // uv_close would instead impose asynchronous lifetime on failed construction.
  // Undo that registration synchronously, retaining application data. Any peer
  // sockets prepared by libuv are owned by the loop, not the poll handle.
  inline int finish_poll_init(uv_poll_t &native, int code) noexcept {
    if (code >= 0 || native.loop == nullptr) return code;
    auto *handle = reinterpret_cast<uv_handle_t *>(&native);
    assert(!uv_is_active(handle));
    assert(!uv_is_closing(handle));
    unlink_poll_init_registration(native.handle_queue);
    auto *data = native.data;
    native = {};
    native.data = data;
    return code;
  }

}
