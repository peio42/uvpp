#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "miscellaneous.hpp"

namespace uv {

  namespace fs {
    struct Rq : Request<uv_fs_t, void> {
      enum class Type {
        Unknown = UV_FS_UNKNOWN,
        Custom = UV_FS_CUSTOM,
        Open = UV_FS_OPEN,
        Close = UV_FS_CLOSE,
        Read = UV_FS_READ,
        Write = UV_FS_WRITE,
        Sendfile = UV_FS_SENDFILE,
        Stat = UV_FS_STAT,
        Lstat = UV_FS_LSTAT,
        Fstat = UV_FS_FSTAT,
        Ftruncate = UV_FS_FTRUNCATE,
        Utime = UV_FS_UTIME,
        Futime = UV_FS_FUTIME,
        Access = UV_FS_ACCESS,
        Chmod = UV_FS_CHMOD,
        Fchmod = UV_FS_FCHMOD,
        Fsync = UV_FS_FSYNC,
        Fdatasync = UV_FS_FDATASYNC,
        Unlink = UV_FS_UNLINK,
        Rmdir = UV_FS_RMDIR,
        Mkdir = UV_FS_MKDIR,
        Mkdtemp = UV_FS_MKDTEMP,
        Rename = UV_FS_RENAME,
        Scandir = UV_FS_SCANDIR,
        Link = UV_FS_LINK,
        Symlink = UV_FS_SYMLINK,
        Readlink = UV_FS_READLINK,
        Chown = UV_FS_CHOWN,
        Fchown = UV_FS_FCHOWN,
        Realpath = UV_FS_REALPATH,
        Copyfile = UV_FS_COPYFILE,
        Lchown = UV_FS_LCHOWN,
        Opendir = UV_FS_OPENDIR,
        Readdir = UV_FS_READDIR,
        Closedir = UV_FS_CLOSEDIR,
        Mkstemp = UV_FS_MKSTEMP,
#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 36
        Lutime = UV_FS_LUTIME,
#endif
      };

      void cleanup() {
        uv_fs_req_cleanup(this);
      }

      Loop *getLoop() {
        return static_cast<Loop *>(this->loop);
      }

      Type getType() {
        return static_cast<Type>(this->fs_type);
      }
    };

    using Cb = void (*)(Rq *req);

    struct Timespec : uv_timespec_t {
      Timespec() {
        tv_sec = 0;
        tv_nsec = 0;
      }
      Timespec(const long sec) {
        tv_sec = sec;
        tv_nsec = 0;
      }
      Timespec(const long sec, const long nsec) {
        tv_sec = sec;
        tv_nsec = nsec;
      }
      Timespec(const double sec) {
        tv_sec = static_cast<long>(sec);
        tv_nsec = static_cast<long>((sec - tv_sec) * 1e9);
      }

      long sec() const { return tv_sec; }
      long nsec() const { return tv_nsec; }

      double as_double() const {
        return static_cast<double>(tv_sec) + static_cast<double>(tv_nsec) / 1000000000.0;
      }
    };

    using Stat = uv_stat_t;

    using Statfs = uv_statfs_t;


    inline void unlink(const char *path) {
      Error::safe(uv_fs_unlink(nullptr, nullptr, path, nullptr));
    }
    inline void unlink(Loop *loop, Rq *req, const char *path, Cb cb) {
      Error::safe(uv_fs_unlink(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void unlink(Loop *loop, Rq *req, const char *path) {
      unlink(loop, req, path, Error::safeReqCb<Rq, cb>);
    }

    inline void mkdir(const char *path, const int mode) {
      Error::safe(uv_fs_mkdir(nullptr, nullptr, path, mode, nullptr));
    }
    inline void mkdir(Loop *loop, Rq *req, const char *path, int mode, Cb cb) {
      Error::safe(uv_fs_mkdir(loop, req, path, mode, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void mkdir(Loop *loop, Rq *req, const char *path, int mode) {
      mkdir(loop, req, path, mode, Error::safeReqCb<Rq, cb>);
    }

    inline void mkdtemp(const char *path) {
      Error::safe(uv_fs_mkdtemp(nullptr, nullptr, path, nullptr));
    }
    inline void mkdtemp(Loop *loop, Rq *req, const char *tpl, Cb cb) {
      Error::safe(uv_fs_mkdtemp(loop, req, tpl, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void mkdtemp(Loop *loop, Rq *req, const char *tpl) {
      mkdtemp(loop, req, tpl, Error::safeReqCb<Rq, cb>);
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 34
    inline void mkstemp(const char *path) {
      Error::safe(uv_fs_mkstemp(nullptr, nullptr, path, nullptr));
    }
    inline void mkstemp(Loop *loop, Rq *req, const char *tpl, Cb cb) {
      Error::safe(uv_fs_mkstemp(loop, req, tpl, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void mkstemp(Loop *loop, Rq *req, const char *tpl) {
      mkstemp(loop, req, tpl, Error::safeReqCb<Rq, cb>);
    }
#endif

    inline void rmdir(const char *path) {
      Error::safe(uv_fs_rmdir(nullptr, nullptr, path, nullptr));
    }
    inline void rmdir(Loop *loop, Rq *req, const char *path, Cb cb) {
      Error::safe(uv_fs_rmdir(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void rmdir(Loop *loop, Rq *req, const char *path) {
      rmdir(loop, req, path, Error::safeReqCb<Rq, cb>);
    }

    inline void scandir(Loop *loop, Rq *req, const char *path, int flags, Cb cb) {
      Error::safe(uv_fs_scandir(loop, req, path, flags, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void scandir(Loop *loop, Rq *req, const char *path, int flags) {
      scandir(loop, req, path, flags, Error::safeReqCb<Rq, cb>);
    }

    inline void stat(Loop *loop, Rq *req, const char *path, Cb cb = nullptr) {
      Error::safe(uv_fs_stat(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void stat(Loop *loop, Rq *req, const char *path) {
      stat(loop, req, path, Error::safeReqCb<Rq, cb>);
    }

    inline void lstat(Loop *loop, Rq *req, const char *path, Cb cb = nullptr) {
      Error::safe(uv_fs_lstat(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void lstat(Loop *loop, Rq *req, const char *path) {
      lstat(loop, req, path, Error::safeReqCb<Rq, cb>);
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 31
    inline void statfs(Loop *loop, Rq *req, const char *path, Cb cb = nullptr) {
      Error::safe(uv_fs_statfs(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void statfs(Loop *loop, Rq *req, const char *path) {
      fstatfs(loop, req, path, Error::safeReqCb<Rq, cb>);
    }
#endif
  
    inline void rename(const char *path, const char *new_path) {
      Error::safe(uv_fs_rename(nullptr, nullptr, path, new_path, nullptr));
    }
    inline void rename(Loop *loop, Rq *req, const char *path, const char *new_path, Cb cb = nullptr) {
      Error::safe(uv_fs_rename(loop, req, path, new_path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void rename(Loop *loop, Rq *req, const char *path, const char *new_path) {
      rename(loop, req, path, new_path, Error::safeReqCb<Rq, cb>);
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 14
    inline void copyfile(const char *path, const char *new_path, int flags) {
      Error::safe(uv_fs_copyfile(nullptr, nullptr, path, new_path, flags, nullptr));
    }
    inline void copyfile(Loop *loop, Rq *req, const char *path, const char *new_path, int flags, Cb cb = nullptr) {
      Error::safe(uv_fs_copyfile(loop, req, path, new_path, flags, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void copyfile(Loop *loop, Rq *req, const char *path, const char *new_path, int flags) {
      copyfile(loop, req, path, new_path, flags, Error::safeReqCb<Rq, cb>);
    }
#endif

    inline int access(const char *path, int mode) {
      return Error::safe(uv_fs_access(nullptr, nullptr, path, mode, nullptr));
    }
    inline void access(Loop *loop, Rq *req, const char *path, int mode, Cb cb = nullptr) {
      Error::safe(uv_fs_access(loop, req, path, mode, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void access(Loop *loop, Rq *req, const char *path, int mode) {
      access(loop, req, path, mode, Error::safeReqCb<Rq, cb>);
    }

    inline void chmod(const char *path, int mode) {
      Error::safe(uv_fs_chmod(nullptr, nullptr, path, mode, nullptr));
    }
    inline void chmod(Loop *loop, Rq *req, const char *path, int mode, Cb cb = nullptr) {
      Error::safe(uv_fs_chmod(loop, req, path, mode, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void chmod(Loop *loop, Rq *req, const char *path, int mode) {
      chmod(loop, req, path, mode, Error::safeReqCb<Rq, cb>);
    }

    inline void utime(const char *path, double atime, double mtime) {
      Error::safe(uv_fs_utime(nullptr, nullptr, path, atime, mtime, nullptr));
    }
    inline void utime(Loop *loop, Rq *req, const char *path, double atime, double mtime, Cb cb = nullptr) {
      Error::safe(uv_fs_utime(loop, req, path, atime, mtime, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void utime(Loop *loop, Rq *req, const char *path, double atime, double mtime) {
      utime(loop, req, path, atime, mtime, Error::safeReqCb<Rq, cb>);
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 36
    inline void lutime(const char *path, double atime, double mtime) {
      Error::safe(uv_fs_lutime(nullptr, nullptr, path, atime, mtime, nullptr));
    }
    inline void lutime(Loop *loop, Rq *req, const char *path, double atime, double mtime, Cb cb) {
      Error::safe(uv_fs_lutime(loop, req, path, atime, mtime, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void lutime(Loop *loop, Rq *req, const char *path, double atime, double mtime) {
      lutime(loop, req, path, atime, mtime, Error::safeReqCb<Rq, cb>);
    }
#endif

    inline void link(const char *path, const char *new_path) {
      Error::safe(uv_fs_link(nullptr, nullptr, path, new_path, nullptr));
    }
    inline void link(Loop *loop, Rq *req, const char *path, const char *new_path, Cb cb) {
      Error::safe(uv_fs_link(loop, req, path, new_path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void link(Loop *loop, Rq *req, const char *path, const char *new_path) {
      link(loop, req, path, new_path, Error::safeReqCb<Rq, cb>);
    }

    inline void symlink(const char *path, const char *new_path, int flags) {
      Error::safe(uv_fs_symlink(nullptr, nullptr, path, new_path, flags, nullptr));
    }
    inline void symlink(Loop *loop, Rq *req, const char *path, const char *new_path, int flags, Cb cb) {
      Error::safe(uv_fs_symlink(loop, req, path, new_path, flags, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void symlink(Loop *loop, Rq *req, const char *path, const char *new_path, int flags) {
      symlink(loop, req, path, new_path, flags, Error::safeReqCb<Rq, cb>);
    }

    using ReadlinkCb = void (*)(Rq *req, const char *target);
    inline void readlink(Loop *loop, Rq *req, const char *path, ReadlinkCb cb) {
      Error::safe(uv_fs_readlink(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<ReadlinkCb cb>
    inline void readlink(Loop *loop, Rq *req, const char *path) {
      Error::safe(uv_fs_readlink(loop, req, path, [](uv_fs_t *req) {
        Error::safe(req->result);
        cb(reinterpret_cast<Rq *>(req), static_cast<char *>(req->ptr));
      }));
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 8
    using RealpathCb = void (*)(Rq *req, const char *target);
    inline void realpath(Loop *loop, Rq *req, const char *path, RealpathCb cb) {
      Error::safe(uv_fs_realpath(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<RealpathCb cb>
    inline void realpath(Loop *loop, Rq *req, const char *path) {
      Error::safe(uv_fs_realpath(loop, req, path, [](uv_fs_t *req) {
        Error::safe(req->result);
        cb(reinterpret_cast<Rq *>(req), static_cast<char *>(req->ptr));
      }));
    }
#endif

    inline void chown(const char *path, int uid, int gid) {
      Error::safe(uv_fs_chown(nullptr, nullptr, path, uid, gid, nullptr));
    }
    inline void chown(Loop *loop, Rq *req, const char *path, int uid, int gid, Cb cb) {
      Error::safe(uv_fs_chown(loop, req, path, uid, gid, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void chown(Loop *loop, Rq *req, const char *path, int uid, int gid) {
      chown(loop, req, path, uid, gid, Error::safeReqCb<Rq, cb>);
    }

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 21
    inline void lchown(const char *path, int uid, int gid) {
      Error::safe(uv_fs_lchown(nullptr, nullptr, path, uid, gid, nullptr));
    }
    inline void lchwon(Loop *loop, Rq *req, const char *path, int uid, int gid, Cb cb) {
      Error::safe(uv_fs_lchown(loop, req, path, uid, gid, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    inline void lchown(Loop *loop, Rq *req, const char *path, int uid, int gid) {
      lchown(loop, req, path, uid, gid, Error::safeReqCb<Rq, cb>);
    }
#endif

  };

  struct File {
    uv_file fd;

    File() : fd(-1) {}
    File(const uv_file fd) : fd(fd) {}
    File(const File &other) : fd(other.fd) {}
    //File(File &&other) : fd(other.fd) { other.fd = -1; }
    File &operator = (const File &other) { fd = other.fd; return *this; }
    //File &operator = (File &&other) { fd = other.fd; other.fd = -1; return *this; }
    //~File() {
    //  if (fd >= 0) {
    //    uv_fs_close(nullptr, nullptr, fd, nullptr);
    //    fd = -1;
    //  }
    //}
    bool isOpen() const { return fd >= 0; }

    operator uv_file() const { return fd; }
    operator bool() const { return isOpen(); }

    using Rq = fs::Rq;
    using Cb = fs::Cb;

    static int open(Loop *loop, Rq *req, const char *path, int flags, int mode, Cb cb) {
      return Error::safe(uv_fs_open(loop, req, path, flags, mode, reinterpret_cast<uv_fs_cb>(cb)));
    }
    using OpenCb = void (*)(Rq *req, File file);
    template<OpenCb cb>
    static void open(Loop *loop, Rq *req, const char *path, int flags, int mode) {
      uv_fs_open(loop, req, path, flags, mode, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req), File(req->result));
        }));
    }

    void close(Loop *loop, Rq *req, Cb cb = nullptr) {
      Error::safe(uv_fs_close(loop, req, fd, reinterpret_cast<uv_fs_cb>(cb)));
      fd = -1;
    }
    template<Cb cb>
    void close(Loop *loop, Rq *req) {
      close(loop, req, Error::safeReqCb<Rq, cb>);
    }

    using ReadCb = void (*)(Rq *req, const ssize_t nread);
    template<ReadCb cb>
    void read(Loop *loop, Rq *req, const Buffer bufs[], const unsigned int nbufs, const int64_t offset) {
      uv_fs_read(loop, req, fd, bufs, nbufs, offset, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req), req->result);
        }));
    }

    using WriteCb = void (*)(Rq *req, const ssize_t nread);
    template<WriteCb cb>
    void write(Loop *loop, Rq *req, const Buffer bufs[], const unsigned int nbufs, const int64_t offset) {
      uv_fs_write(loop, req, fd, bufs, nbufs, offset, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req), req->result);
        }));
    }

    template<Cb cb>
    void stat(Loop *loop, Rq *req) {
      uv_fs_fstat(loop, req, fd, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req));
        }));
    }

    template<Cb cb>
    void sync(Loop *loop, Rq *req) {
      uv_fs_fsync(loop, req, fd, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req));
        }));
    }

    template<Cb cb>
    void datasync(Loop *loop, Rq *req) {
      uv_fs_fdatasync(loop, req, fd, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req));
        }));
    }

    template<Cb cb>
    void truncate(Loop *loop, Rq *req, const int64_t offset) {
      uv_fs_ftruncate(loop, req, fd, offset, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req));
        }));
    }

    template<Cb cb>
    void sendfile(Loop *loop, Rq *req, File out, const int64_t in_offset, const size_t length) {
      uv_fs_sendfile(loop, req, out, fd, in_offset, length, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req));
        }));
    }

    template<Cb cb>
    void chmod(Loop *loop, Rq *req, const int mode) {
      uv_fs_fchmod(loop, req, fd, mode, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req));
        }));
    }

    template<Cb cb>
    void utime(Loop *loop, Rq *req, const double atime, const double mtime) {
      uv_fs_futime(loop, req, fd, atime, mtime, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req));
        }));
    }

    template<Cb cb>
    void chown(Loop *loop, Rq *req, const int uid, const int gid) {
      uv_fs_fchown(loop, req, fd, uid, gid, reinterpret_cast<uv_fs_cb>([](uv_fs_t *req) {
          Error::safe(req->result);
          cb(static_cast<Rq *>(req));
        }));
    }
  };

#if UV_VERSION_MAJOR >= 1 && UV_VERSION_MINOR >= 28
  struct Dir : uv_dir_t {

    struct Entry : uv_dirent_t {
      enum Type {
        Unknown = UV_DIRENT_UNKNOWN,
        File = UV_DIRENT_FILE,
        Dir = UV_DIRENT_DIR,
        Link = UV_DIRENT_LINK,
        Fifo = UV_DIRENT_FIFO,
        Socket = UV_DIRENT_SOCKET,
        Char = UV_DIRENT_CHAR,
        Block = UV_DIRENT_BLOCK
      };

      Type getType() const {
        return static_cast<Type>(type);
      }
      bool isFile() const { return type == UV_DIRENT_FILE; }
      bool isDir() const { return type == UV_DIRENT_DIR; }
      bool isLink() const { return type == UV_DIRENT_LINK; }
      bool isFifo() const { return type == UV_DIRENT_FIFO; }
      bool isSocket() const { return type == UV_DIRENT_SOCKET; }
      bool isChar() const { return type == UV_DIRENT_CHAR; }
      bool isBlock() const { return type == UV_DIRENT_BLOCK; }
    };

    Dir(Entry _dirents[], const unsigned int _nentries) {
      dirents = static_cast<uv_dirent_t *>(_dirents);
      nentries = _nentries;
    }
    Dir(const unsigned int _nentries) {
      dirents = new Entry[_nentries];
      nentries = _nentries;
    }

    using Rq = fs::Rq;
    using Cb = fs::Cb;

    static void open(Loop *loop, Rq *req, const char *path, Cb cb) {
      Error::safe(uv_fs_opendir(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    using OpendirCb = void (*)(Rq *req, Dir *dir);
    template<OpendirCb cb>
    static void open(Loop *loop, Rq *req, const char *path) {
      open(loop, req, path, Error::safeReqCb<Rq, cb>);
    }

    Entry &operator[](const size_t index) {
      return static_cast<Entry &>(dirents[index]);
    }
    Entry *entry(const size_t index) {
      if (index >= nentries)
        return nullptr;
      return & operator[](index);
    }

    void close(Loop *loop, Rq *req, Cb cb) {
      Error::safe(uv_fs_closedir(loop, req, this, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    void close(Loop *loop, Rq *req) {
      close(loop, req, Error::safeReqCb<Rq, cb>);
    }

    void read(Loop *loop, Rq *req, Cb cb) {
      Error::safe(uv_fs_readdir(loop, req, this, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    void read(Loop *loop, Rq *req) {
      read(loop, req, Error::safeReqCb<Rq, cb>);
    }

    struct ScandirRq : fs::Rq {
      bool next(Entry *entry) {
        auto res = uv_fs_scandir_next(this, entry);
        if (res == UV_EOF)
          return false;
        Error::safe(res);
        return true;
      }
    };
    using ScandirCb = void (*)(ScandirRq *req);
    static void scandir(Loop *loop, ScandirRq *req, const char *path, Cb cb) {
      Error::safe(uv_fs_scandir(loop, req, path, 0, reinterpret_cast<uv_fs_cb>(cb)));
    }
  };
#endif

}
