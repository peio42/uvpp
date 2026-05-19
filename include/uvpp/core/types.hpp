#pragma once

#include <uv.h>

namespace uv {

  enum class run_mode {
    until_done = UV_RUN_DEFAULT,
    once = UV_RUN_ONCE,
    nowait = UV_RUN_NOWAIT
  };

  enum class handle_type {
    unknown = UV_UNKNOWN_HANDLE,
    async = UV_ASYNC,
    check = UV_CHECK,
    fs_event = UV_FS_EVENT,
    fs_poll = UV_FS_POLL,
    handle = UV_HANDLE,
    idle = UV_IDLE,
    pipe = UV_NAMED_PIPE,
    poll = UV_POLL,
    prepare = UV_PREPARE,
    process = UV_PROCESS,
    stream = UV_STREAM,
    tcp = UV_TCP,
    timer = UV_TIMER,
    tty = UV_TTY,
    udp = UV_UDP,
    signal = UV_SIGNAL,
    file = UV_FILE
  };

  enum class request_type {
    unknown = UV_UNKNOWN_REQ,
    request = UV_REQ,
    connect = UV_CONNECT,
    write = UV_WRITE,
    shutdown = UV_SHUTDOWN,
    udp_send = UV_UDP_SEND,
    fs = UV_FS,
    work = UV_WORK,
    getaddrinfo = UV_GETADDRINFO,
    getnameinfo = UV_GETNAMEINFO,
    random = UV_RANDOM
  };

  enum class fs_type {
    unknown = UV_FS_UNKNOWN,
    custom = UV_FS_CUSTOM,
    open = UV_FS_OPEN,
    close = UV_FS_CLOSE,
    read = UV_FS_READ,
    write = UV_FS_WRITE,
    sendfile = UV_FS_SENDFILE,
    stat = UV_FS_STAT,
    lstat = UV_FS_LSTAT,
    fstat = UV_FS_FSTAT,
    ftruncate = UV_FS_FTRUNCATE,
    utime = UV_FS_UTIME,
    futime = UV_FS_FUTIME,
    access = UV_FS_ACCESS,
    chmod = UV_FS_CHMOD,
    fchmod = UV_FS_FCHMOD,
    fsync = UV_FS_FSYNC,
    fdatasync = UV_FS_FDATASYNC,
    unlink = UV_FS_UNLINK,
    rmdir = UV_FS_RMDIR,
    mkdir = UV_FS_MKDIR,
    mkdtemp = UV_FS_MKDTEMP,
    rename = UV_FS_RENAME,
    scandir = UV_FS_SCANDIR,
    link = UV_FS_LINK,
    symlink = UV_FS_SYMLINK,
    readlink = UV_FS_READLINK,
    chown = UV_FS_CHOWN,
    fchown = UV_FS_FCHOWN,
    realpath = UV_FS_REALPATH,
    copyfile = UV_FS_COPYFILE,
    lchown = UV_FS_LCHOWN,
    opendir = UV_FS_OPENDIR,
    readdir = UV_FS_READDIR,
    closedir = UV_FS_CLOSEDIR,
    statfs = UV_FS_STATFS,
    mkstemp = UV_FS_MKSTEMP,
    lutime = UV_FS_LUTIME
  };

} // namespace uv
