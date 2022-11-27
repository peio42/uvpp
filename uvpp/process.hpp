#pragma once

#include "uv.h"
#include "error.hpp"
#include "handle.hpp"
#include "loop.hpp"

namespace uv {

  struct Process : THandle<uv_process_t, Process> {
    enum class Flags : unsigned int {
      SetUID = UV_PROCESS_SETUID,
      SetGID = UV_PROCESS_SETGID,
      WindowsVerbatimArguments = UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS,
      Detached = UV_PROCESS_DETACHED,
      WindowsHide = UV_PROCESS_WINDOWS_HIDE,
#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 24
      WindowsHideConsole = UV_PROCESS_WINDOWS_HIDE_CONSOLE,
      WindowsHideGUI = UV_PROCESS_WINDOWS_HIDE_GUI,
#endif
    };

    using StdioContainer = uv_stdio_container_t;

    enum class StdioFlags {
      Ignore = UV_IGNORE,
      CreatePipe = UV_CREATE_PIPE,
      InheritFd = UV_INHERIT_FD,
      InheritStream = UV_INHERIT_STREAM,
      ReadablePipe = UV_READABLE_PIPE,
      WritablePipe = UV_WRITABLE_PIPE,
#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 41
      NonblockPipe = UV_NONBLOCK_PIPE,
#endif
    };

    using ExitCb = void (*)(Process *handle, int64_t exit_status, int term_signal);
    struct Options : uv_process_options_t {
      Options() {
        exit_cb = nullptr;
        file = nullptr;
        flags = 0;
        stdio_count = 0;
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

      Options &SetStdio(StdioContainer *_stdio, int _stdio_count) {
        stdio = _stdio;
        stdio_count = _stdio_count;
        return *this;
      }

      Options &setUid(int _uid) {
        flags |= UV_PROCESS_SETUID;
        uid = _uid;
        return *this;
      }

      Options &setGid(int _gid) {
        flags |= UV_PROCESS_SETGID;
        gid = _gid;
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
      Error::safe(uv_spawn(loop, this, options));
    }

    // template<class V = void>
    // Process(Loop *loop, const Options *options, V *data = nullptr) {
    //   uv_spawn(loop, this, options);
    //   this->data = static_cast<void *>(data);
    // }

    void kill(int signum) {
      Error::safe(uv_process_kill(this, signum));
    }

    static void kill(int pid, int signum) {
      Error::safe(uv_kill(pid, signum));
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 19
    uv_pid_t getPid() {
      return uv_process_get_pid(this);
    }
#endif

  };

}
