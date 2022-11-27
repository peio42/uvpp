#pragma once

#include "uv.h"
#include "error.h"
#include "loop.hpp"
#include "request.hpp"
#include "handle.hpp"

namespace uv {

  struct FsEvent : THandle<uv_fs_event_t, FsEvent> {
    enum class Event : unsigned int {
      Rename = UV_RENAME,
      Change = UV_CHANGE,
    };

    enum class EventFlags : unsigned int {
      None = 0,
      WatchEntry = UV_FS_EVENT_WATCH_ENTRY,
      Stat = UV_FS_EVENT_STAT,
      Recursive = UV_FS_EVENT_RECURSIVE,
    };


    bool castable(Handle &h) { return h.getType() == Handle::Type::FsEvent; }


    FsEvent(Loop *loop) {
      Error::safe(uv_fs_event_init(loop, this));
    }

    using StartCb = void (*)(FsEvent *handle, const char *filename, Event events, int status);
    using SafeStartCb = void (*)(FsEvent *handle, const char *filename, Event events);
    void start(StartCb cb, const char *path, EventFlags flags = EventFlags::None) {
      Error::safe(uv_fs_event_start(this, reinterpret_cast<uv_fs_event_cb>(cb), path, static_cast<unsigned int>(flags)));
    }
    template<SafeStartCb cb>
    void start(const char *path, EventFlags flags = EventFlags::None) {
      start([](FsEvent *handle, const char *filename, Event events, int status) {
          Error::safe(status);
          cb(handle, filename, events);
        }, path, flags);
    }

    void stop() {
      Error::safe(uv_fs_event_stop(this));
    }

    void getpath(char *buffer, size_t *size) {
      Error::safe(uv_fs_event_getpath(this, buffer, size));
    }
  };

  inline FsEvent::Event operator | (FsEvent::Event lhs, FsEvent::Event rhs) {
    return static_cast<FsEvent::Event>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
  }
  inline FsEvent::Event operator & (FsEvent::Event lhs, FsEvent::Event rhs) {
    return static_cast<FsEvent::Event>(static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs));
  }
  inline bool operator % (FsEvent::Event lhs, FsEvent::Event rhs) {
    return (static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs)) != 0;
  }

  inline FsEvent::EventFlags operator | (FsEvent::EventFlags lhs, FsEvent::EventFlags rhs) {
    return static_cast<FsEvent::EventFlags>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
  }
  inline FsEvent::EventFlags operator & (FsEvent::EventFlags lhs, FsEvent::EventFlags rhs) {
    return static_cast<FsEvent::EventFlags>(static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs));
  }
  inline bool operator % (FsEvent::EventFlags lhs, FsEvent::EventFlags rhs) {
    return (static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs)) != 0;
  }

}
