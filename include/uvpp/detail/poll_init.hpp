#pragma once

#include <cassert>
#include <type_traits>

#include <uv.h>

namespace uv::detail {

  // uvpp helper, not an exported libuv function: it reproduces queue removal.
  // Audited Windows tags and prerequisites are listed in
  // docs/design/raw-poll-init-rollback.md. No blanket libuv 1.x guarantee.
  // 1.44.2/1.45.0 use void*[2]; audited tags from 1.46.0 use uv__queue.
  // This is deliberately confined to rollback of an uncommitted poll init.
  template<class Queue>
  void unlink_poll_init_registration(Queue &queue) noexcept {
    if constexpr (requires { queue.next; queue.prev; }) {
      queue.next->prev = queue.prev;
      queue.prev->next = queue.next;
    } else {
      static_assert(std::is_array_v<Queue> && std::extent_v<Queue> == 2 &&
                    std::is_same_v<std::remove_extent_t<Queue>, void *>,
                    "Unreviewed libuv registration queue; audit poll-init rollback");
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
