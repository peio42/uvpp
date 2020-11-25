#pragma once

#include "uv.h"
#include "error.hpp"
#include "loop.hpp"
#include "request.hpp"
#include "miscellaneous.hpp"

namespace uv {

  struct fs {
    struct Rq;

    using Cb = void (*)(Rq *req);

    struct Rq : Request<uv_fs_t, void> {
      void cleanup() {
        uv_fs_req_cleanup(this);
      }
    };

    using Timespec = uv_timespec_t;

    using Stat = uv_stat_t;

    using Statfs = uv_statfs_t;

    using Dir = uv_dir_t;


    static void close(Loop *loop, Rq *req, uv_file file, Cb cb = nullptr) {
      _safe(uv_fs_close(loop, req, file, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void close(Loop *loop, Rq *req, uv_file file) {
      close(loop, req, file, _safeReqCb<Rq, cb>);
    }

    static int open(Loop *loop, Rq *req, const char *path, int flags, int mode, Cb cb) {
      return _safe(uv_fs_open(loop, req, path, flags, mode, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static int open(Loop *loop, Rq *req, const char *path, int flags, int mode) {
      return open(loop, req, path, flags, mode, _safeReqCb<Rq, cb>);
    }

    static int read(Loop *loop, Rq *req, uv_file file, const Buffer bufs[], unsigned int nbufs, int64_t offset, Cb cb) {
      return _safe(uv_fs_read(loop, req, file, bufs, nbufs, offset, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb rcb, Cb ecb>
    static int read(Loop *loop, Rq *req, uv_file file, const Buffer bufs[], unsigned int nbufs, int64_t offset) {
      return read(loop, req, file, bufs, nbufs, offset, [](Rq *req) {
          _safe(req->result);
          if (req->result > 0)
            rcb(req);
          else
            ecb(req);
        });
    }

    static void unlink(Loop *loop, Rq *req, const char *path, Cb cb) {
      _safe(uv_fs_unlink(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void unlink(Loop *loop, Rq *req, const char *path) {
      unlink(loop, req, path, _safeReqCb<Rq, cb>);
    }

    static int write(Loop *loop, Rq *req, uv_file file, const Buffer bufs[], unsigned int nbufs, int64_t offset, Cb cb) {
      return _safe(uv_fs_write(loop, req, file, bufs, nbufs, offset, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static int write(Loop *loop, Rq *req, uv_file file, const Buffer bufs[], unsigned int nbufs, int64_t offset) {
      return write(loop, req, file, bufs, nbufs, offset, _safeReqCb<Rq, cb>);
    }

    static void mkdir(Loop *loop, Rq *req, const char *path, int mode, Cb cb) {
      _safe(uv_fs_mkdir(loop, req, path, mode, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void mkdir(Loop *loop, Rq *req, const char *path, int mode) {
      mkdir(loop, req, path, mode, _safeReqCb<Rq, cb>);
    }

    static void mkdtemp(Loop *loop, Rq *req, const char *tpl, Cb cb) {
      _safe(uv_fs_mkdtemp(loop, req, tpl, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void mkdtemp(Loop *loop, Rq *req, const char *tpl) {
      mkdtemp(loop, req, tpl, _safeReqCb<Rq, cb>);
    }

    static void mkstemp(Loop *loop, Rq *req, const char *tpl, Cb cb) {
      _safe(uv_fs_mkstemp(loop, req, tpl, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void mkstemp(Loop *loop, Rq *req, const char *tpl) {
      mkstemp(loop, req, tpl, _safeReqCb<Rq, cb>);
    }

    static void rmdir(Loop *loop, Rq *req, const char *path, Cb cb) {
      _safe(uv_fs_rmdir(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void rmdir(Loop *loop, Rq *req, const char *path) {
      rmdir(loop, req, path, _safeReqCb<Rq, cb>);
    }

    static void opendir(Loop *loop, Rq *req, const char *path, Cb cb) {
      _safe(uv_fs_opendir(loop, req, path, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void opendir(Loop *loop, Rq *req, const char *path) {
      opendir(loop, req, path, _safeReqCb<Rq, cb>);
    }

    static void closedir(Loop *loop, Rq *req, uv_dir_t *dir, Cb cb) {
      _safe(uv_fs_closedir(loop, req, dir, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void closedir(Loop *loop, Rq *req, uv_dir_t *dir) {
      closedir(loop, req, dir, _safeReqCb<Rq, cb>);
    }

    static int readdir(Loop *loop, Rq *req, uv_dir_t *dir, Cb cb) {
      return _safe(uv_fs_readdir(loop, req, dir, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static int readdir(Loop *loop, Rq *req, uv_dir_t *dir) {
      readdir(loop, req, dir, _safeReqCb<Rq, cb>);
    }

    static void scandir(Loop *loop, Rq *req, const char *path, int flags, Cb cb) {
      _safe(uv_fs_scandir(loop, req, path, flags, reinterpret_cast<uv_fs_cb>(cb)));
    }
    template<Cb cb>
    static void scandir(Loop *loop, Rq *req, const char *path, int flags) {
      scandir(loop, req, path, flags, _safeReqCb<Rq, cb>);
    }

    static bool scandir_next(Rq *req, uv_dirent_t *ent) {
      auto res = uv_fs_scandir_next(req, ent);
      if (res == UV_EOF)
        return false;
      _safe(res);
      return true;
    }
  };

}
