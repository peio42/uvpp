#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Process : THandle<uv_process_t, Process> {
    using ExitCb = void (*)(Process *handle, int64_t exit_status, int term_signal);

    enum class Flags : unsigned int {
      SetUID = UV_PROCESS_SETUID,
      SetGID = UV_PROCESS_SETGID,
      WindowsVerbatimArguments = UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS,
      Detached = UV_PROCESS_DETACHED,
      WindowsHide = UV_PROCESS_WINDOWS_HIDE,
      WindowsHideConsole = UV_PROCESS_WINDOWS_HIDE_CONSOLE,
      WindowsHideGUI = UV_PROCESS_WINDOWS_HIDE_GUI,
    };

    using StdioContainer = uv_stdio_container_t;

    enum class StdioFlags {
      Ignore = UV_IGNORE,
      Create_Pipe = UV_CREATE_PIPE,
      Inherit_Fd = UV_INHERIT_FD,
      Inherit_Stream = UV_INHERIT_STREAM,
      Readable_Pipe = UV_READABLE_PIPE,
      Writable_Pipe = UV_WRITABLE_PIPE,
      Overlapped_Pipe = UV_OVERLAPPED_PIPE,
    };

    struct Options : uv_process_options_t {
      Options() {
        exit_cb = nullptr;
        file = nullptr;
        flags = 0;
      }

      Options &onExit(ExitCb cb) {
        exit_cb = reinterpret_cast<uv_exit_cb>(cb);
        return *this;
      }
      template<ExitCb cb>
      Options &onExit() {
        return onExit(cb);
      }

      Options &setFileArgs(const char *_file, char *_args[]) {
        file = _file;
        args = _args;
        return *this;
      }

      Options &setCwd(char *_cwd) {
        cwd = _cwd;
        return *this;
      }

      Options &setEnv(char *_env[]) {
        env = _env;
        return *this;
      }

      Options &setUid(int _uid) {
        flags |= UV_PROCESS_SETUID;
        uid = _uid;
        return *this;
      }

      Options &setGid(int _gid) {
        flags |= UV_PROCESS_SETGID;
        uid = _gid;
        return *this;
      }

      Options &setWindowsVerbatimArguments() {
        flags |= UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS;
        return *this;
      }

      Options &setDetached() {
        flags |= UV_PROCESS_DETACHED;
        return *this;
      }
    };


    bool castable(Handle &h) { return h.getType() == Handle::Type::Process; }


    static void disable_stdio_inheritance() {
      uv_disable_stdio_inheritance();
    }

    Process(Loop *loop, const Options *options) {
      _safe(uv_spawn(loop, this, options));
    }

    // template<class V = void>
    // Process(Loop *loop, const Options *options, V *data = nullptr) {
    //   uv_spawn(loop, this, options);
    //   this->data = static_cast<void *>(data);
    // }

    void kill(int signum) {
      _safe(uv_process_kill(this, signum));
    }

    static void kill(int pid, int signum) {
      _safe(uv_kill(pid, signum));
    }

    uv_pid_t pid() {
      return uv_process_get_pid(this);
    }
  };

}
