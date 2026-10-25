#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/fs/file.hpp"
#include "uvpp/fs/result.hpp"
#include "uvpp/fs/status.hpp"
#include "uvpp/core/buffer.hpp"
#include "uvpp/requests/fs.hpp"

namespace uv::raw::fs {

  using open_callback = std::function<void(request&, open_result)>;
  using status_callback = std::function<void(request&, status_result)>;
  using byte_count_callback = std::function<void(request&, byte_count_result)>;
  using stat_callback = std::function<void(request&, stat_result)>;
  using path_callback = std::function<void(request&, path_result)>;
  using temp_file_callback = std::function<void(request&, temp_file_result)>;
  using statfs_callback = std::function<void(request&, statfs_result)>;
  using scandir_callback = std::function<void(request&, scandir_result)>;
  using opendir_callback = std::function<void(request&, opendir_result)>;
  using readdir_callback = std::function<void(request&, readdir_result)>;

  enum class copyfile_flag : int {
    exclusive = UV_FS_COPYFILE_EXCL,
    clone = UV_FS_COPYFILE_FICLONE,
    clone_force = UV_FS_COPYFILE_FICLONE_FORCE
  };

  enum class symlink_flag : int {
    dir = UV_FS_SYMLINK_DIR,
    junction = UV_FS_SYMLINK_JUNCTION
  };

  constexpr int operator|(copyfile_flag lhs, copyfile_flag rhs) noexcept {
    return static_cast<int>(lhs) | static_cast<int>(rhs);
  }

  constexpr int operator|(int lhs, copyfile_flag rhs) noexcept {
    return lhs | static_cast<int>(rhs);
  }

  constexpr int operator|(symlink_flag lhs, symlink_flag rhs) noexcept {
    return static_cast<int>(lhs) | static_cast<int>(rhs);
  }

  constexpr int operator|(int lhs, symlink_flag rhs) noexcept {
    return lhs | static_cast<int>(rhs);
  }

  namespace detail {

    inline ::uv::status submit(request &req, int status) noexcept {
      if (status < 0) {
        req.set_callback({});
      }
      return ::uv::status::from_native(status);
    }

    inline void trampoline(uv_fs_t *raw) noexcept {
      request::from_native(raw).invoke();
    }

    template<class Callback, class ResultFactory>
    void set_callback(request &req, Callback callback, ResultFactory result_factory) {
      req.set_callback([callback = std::move(callback), result_factory = std::move(result_factory)](request &callback_req) mutable {
        auto result = result_factory(callback_req);
        callback(callback_req, std::move(result));
      });
    }

    inline open_result make_open_result(request &request) noexcept {
      return open_result{request.raw_result()};
    }

    inline status_result make_status_result(request &request) noexcept {
      return status_result{request.raw_result()};
    }

    inline byte_count_result make_byte_count_result(request &request) noexcept {
      return byte_count_result{request.raw_result()};
    }

    inline stat_result make_stat_result(request &request) noexcept {
      return stat_result{request.raw_result(), request.statbuf()};
    }

    inline path_result make_path_result(request &request) noexcept {
      return path_result{request.raw_result(), static_cast<const char *>(request.ptr())};
    }

    inline path_result make_request_path_result(request &request) noexcept {
      return path_result{request.raw_result(), request.path()};
    }

    inline temp_file_result make_temp_file_result(request &request) noexcept {
      return temp_file_result{request.raw_result(), request.path()};
    }

    inline statfs_result make_statfs_result(request &request) noexcept {
      return statfs_result{request.raw_result(), static_cast<const uv_statfs_t *>(request.ptr())};
    }

    inline scandir_result make_scandir_result(request &request) noexcept {
      return scandir_result{request.raw_result(), request.native()};
    }

    inline opendir_result make_opendir_result(request &request) noexcept {
      return opendir_result{request.raw_result(), request.ptr()};
    }

    inline readdir_result make_readdir_result(request &request) noexcept {
      return readdir_result{request.raw_result()};
    }

    template<class Submit>
    ::uv::status submit_status(request &request, status_callback callback, Submit submit) {
      set_callback(request, std::move(callback), make_status_result);
      return detail::submit(request, submit(detail::trampoline));
    }

    template<class Submit>
    ::uv::status submit_stat(request &request, stat_callback callback, Submit submit) {
      set_callback(request, std::move(callback), make_stat_result);
      return detail::submit(request, submit(detail::trampoline));
    }

    template<class Submit>
    ::uv::status submit_statfs(request &request, statfs_callback callback, Submit submit) {
      set_callback(request, std::move(callback), make_statfs_result);
      return detail::submit(request, submit(detail::trampoline));
    }

    template<auto Callback, class Submit>
    ::uv::status submit_status_static(request &request, Submit submit) {
      return detail::submit(request, submit([](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_status_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
    }

    template<auto Callback, class Submit>
    ::uv::status submit_stat_static(request &request, Submit submit) {
      return detail::submit(request, submit([](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_stat_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
    }

    template<auto Callback, class Submit>
    ::uv::status submit_statfs_static(request &request, Submit submit) {
      return detail::submit(request, submit([](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_statfs_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
    }

    template<class Submit>
    ::uv::status submit_closedir(directory &dir, Submit submit) {
      auto *native_dir = dir.native();
      auto result = submit(native_dir);
      if (result) {
        dir.release();
      }
      return result;
    }

  }

  inline ::uv::status open(loop_view loop, request &request, std::string_view path, int flags, int mode,
                   open_callback callback) {
    std::string storage{path};
    detail::set_callback(request, std::move(callback), detail::make_open_result);
    return detail::submit(request, uv_fs_open(loop.native(), request.native(), storage.c_str(), flags, mode,
                                       detail::trampoline));
  }

  inline ::uv::status open(loop &loop, request &request, std::string_view path, int flags, int mode,
                   open_callback callback) {
    return open(loop_view{loop.native()}, request, path, flags, mode, std::move(callback));
  }

  template<auto Callback>
  ::uv::status open_static(loop_view loop, request &request, std::string_view path, int flags, int mode) {
    std::string storage{path};
    return detail::submit(request, uv_fs_open(loop.native(), request.native(), storage.c_str(), flags, mode,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_open_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status open_static(loop &loop, request &request, std::string_view path, int flags, int mode) {
    return open_static<Callback>(loop_view{loop.native()}, request, path, flags, mode);
  }

  inline ::uv::status close(loop_view loop, request &request, file_descriptor file, status_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_status_result);
    return detail::submit(request, uv_fs_close(loop.native(), request.native(), file.native(), detail::trampoline));
  }

  inline ::uv::status close(loop &loop, request &request, file_descriptor file, status_callback callback) {
    return close(loop_view{loop.native()}, request, file, std::move(callback));
  }

  template<auto Callback>
  ::uv::status close_static(loop_view loop, request &request, file_descriptor file) {
    return detail::submit(request, uv_fs_close(loop.native(), request.native(), file.native(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_status_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status close_static(loop &loop, request &request, file_descriptor file) {
    return close_static<Callback>(loop_view{loop.native()}, request, file);
  }

  inline ::uv::status read(loop_view loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                   int64_t offset, byte_count_callback callback) {
    const auto buffer_count = uv::detail::checked_buffer_count(buffers.size());
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    return detail::submit(request, uv_fs_read(loop.native(), request.native(), file.native(),
                                       reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                       buffer_count, offset, detail::trampoline));
  }

  inline ::uv::status read(loop &loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                   int64_t offset, byte_count_callback callback) {
    return read(loop_view{loop.native()}, request, file, buffers, offset, std::move(callback));
  }

  inline ::uv::status read(loop_view loop, request &request, file_descriptor file, const buffer_view &buffer,
                   int64_t offset, byte_count_callback callback) {
    return read(loop, request, file, std::span<const buffer_view>{&buffer, 1}, offset, std::move(callback));
  }

  inline ::uv::status read(loop &loop, request &request, file_descriptor file, const buffer_view &buffer,
                   int64_t offset, byte_count_callback callback) {
    return read(loop_view{loop.native()}, request, file, buffer, offset, std::move(callback));
  }

  inline ::uv::status read(loop_view loop, request &request, file_descriptor file, std::span<std::byte> buffer,
                   int64_t offset, byte_count_callback callback) {
    auto raw = uv::detail::make_native_buffer(reinterpret_cast<char *>(buffer.data()), buffer.size());
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    return detail::submit(request, uv_fs_read(loop.native(), request.native(), file.native(), &raw, 1, offset,
                                       detail::trampoline));
  }

  inline ::uv::status read(loop &loop, request &request, file_descriptor file, std::span<std::byte> buffer,
                   int64_t offset, byte_count_callback callback) {
    return read(loop_view{loop.native()}, request, file, buffer, offset, std::move(callback));
  }

  template<auto Callback>
  ::uv::status read_static(loop_view loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                   int64_t offset) {
    const auto buffer_count = uv::detail::checked_buffer_count(buffers.size());
    return detail::submit(request, uv_fs_read(loop.native(), request.native(), file.native(),
                                       reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                       buffer_count, offset,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_byte_count_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status read_static(loop &loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                   int64_t offset) {
    return read_static<Callback>(loop_view{loop.native()}, request, file, buffers, offset);
  }

  template<auto Callback>
  ::uv::status read_static(loop_view loop, request &request, file_descriptor file, const buffer_view &buffer,
                   int64_t offset) {
    return read_static<Callback>(loop, request, file, std::span<const buffer_view>{&buffer, 1}, offset);
  }

  template<auto Callback>
  ::uv::status read_static(loop &loop, request &request, file_descriptor file, const buffer_view &buffer,
                   int64_t offset) {
    return read_static<Callback>(loop_view{loop.native()}, request, file, buffer, offset);
  }

  inline ::uv::status write(loop_view loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                    int64_t offset, byte_count_callback callback) {
    const auto buffer_count = uv::detail::checked_buffer_count(buffers.size());
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    return detail::submit(request, uv_fs_write(loop.native(), request.native(), file.native(),
                                        reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                        buffer_count, offset, detail::trampoline));
  }

  inline ::uv::status write(loop &loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                    int64_t offset, byte_count_callback callback) {
    return write(loop_view{loop.native()}, request, file, buffers, offset, std::move(callback));
  }

  inline ::uv::status write(loop_view loop, request &request, file_descriptor file, const buffer_view &buffer,
                    int64_t offset, byte_count_callback callback) {
    return write(loop, request, file, std::span<const buffer_view>{&buffer, 1}, offset, std::move(callback));
  }

  inline ::uv::status write(loop &loop, request &request, file_descriptor file, const buffer_view &buffer,
                    int64_t offset, byte_count_callback callback) {
    return write(loop_view{loop.native()}, request, file, buffer, offset, std::move(callback));
  }

  inline ::uv::status write(loop_view loop, request &request, file_descriptor file, std::span<const std::byte> buffer,
                    int64_t offset, byte_count_callback callback) {
    auto raw = uv::detail::make_native_buffer(const_cast<char *>(reinterpret_cast<const char *>(buffer.data())),
                                               buffer.size());
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    return detail::submit(request, uv_fs_write(loop.native(), request.native(), file.native(), &raw, 1, offset,
                                        detail::trampoline));
  }

  inline ::uv::status write(loop &loop, request &request, file_descriptor file, std::span<const std::byte> buffer,
                    int64_t offset, byte_count_callback callback) {
    return write(loop_view{loop.native()}, request, file, buffer, offset, std::move(callback));
  }

  template<auto Callback>
  ::uv::status write_static(loop_view loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                    int64_t offset) {
    const auto buffer_count = uv::detail::checked_buffer_count(buffers.size());
    return detail::submit(request, uv_fs_write(loop.native(), request.native(), file.native(),
                                        reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                        buffer_count, offset,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_byte_count_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status write_static(loop &loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                    int64_t offset) {
    return write_static<Callback>(loop_view{loop.native()}, request, file, buffers, offset);
  }

  template<auto Callback>
  ::uv::status write_static(loop_view loop, request &request, file_descriptor file, const buffer_view &buffer,
                    int64_t offset) {
    return write_static<Callback>(loop, request, file, std::span<const buffer_view>{&buffer, 1}, offset);
  }

  template<auto Callback>
  ::uv::status write_static(loop &loop, request &request, file_descriptor file, const buffer_view &buffer,
                    int64_t offset) {
    return write_static<Callback>(loop_view{loop.native()}, request, file, buffer, offset);
  }

  inline ::uv::status stat(loop_view loop, request &request, std::string_view path, stat_callback callback) {
    std::string storage{path};
    detail::set_callback(request, std::move(callback), detail::make_stat_result);
    return detail::submit(request, uv_fs_stat(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline ::uv::status stat(loop &loop, request &request, std::string_view path, stat_callback callback) {
    return stat(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status stat_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    return detail::submit(request, uv_fs_stat(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_stat_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status stat_static(loop &loop, request &request, std::string_view path) {
    return stat_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline ::uv::status unlink(loop_view loop, request &request, std::string_view path, status_callback callback) {
    std::string storage{path};
    detail::set_callback(request, std::move(callback), detail::make_status_result);
    return detail::submit(request, uv_fs_unlink(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline ::uv::status unlink(loop &loop, request &request, std::string_view path, status_callback callback) {
    return unlink(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status unlink_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    return detail::submit(request, uv_fs_unlink(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_status_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status unlink_static(loop &loop, request &request, std::string_view path) {
    return unlink_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline ::uv::status rename(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                     status_callback callback) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_rename(loop.native(), request.native(), path_storage.c_str(), new_path_storage.c_str(), cb);
    });
  }

  inline ::uv::status rename(loop &loop, request &request, std::string_view path, std::string_view new_path,
                     status_callback callback) {
    return rename(loop_view{loop.native()}, request, path, new_path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status rename_static(loop_view loop, request &request, std::string_view path, std::string_view new_path) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_rename(loop.native(), request.native(), path_storage.c_str(), new_path_storage.c_str(), cb);
    });
  }

  template<auto Callback>
  ::uv::status rename_static(loop &loop, request &request, std::string_view path, std::string_view new_path) {
    return rename_static<Callback>(loop_view{loop.native()}, request, path, new_path);
  }

  inline ::uv::status mkdir(loop_view loop, request &request, std::string_view path, int mode,
                    status_callback callback) {
    std::string storage{path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_mkdir(loop.native(), request.native(), storage.c_str(), mode, cb);
    });
  }

  inline ::uv::status mkdir(loop &loop, request &request, std::string_view path, int mode,
                    status_callback callback) {
    return mkdir(loop_view{loop.native()}, request, path, mode, std::move(callback));
  }

  template<auto Callback>
  ::uv::status mkdir_static(loop_view loop, request &request, std::string_view path, int mode) {
    std::string storage{path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_mkdir(loop.native(), request.native(), storage.c_str(), mode, cb);
    });
  }

  template<auto Callback>
  ::uv::status mkdir_static(loop &loop, request &request, std::string_view path, int mode) {
    return mkdir_static<Callback>(loop_view{loop.native()}, request, path, mode);
  }

  inline ::uv::status mkdtemp(loop_view loop, request &request, std::string_view tpl, path_callback callback) {
    std::string storage{tpl};
    detail::set_callback(request, std::move(callback), detail::make_request_path_result);
    return detail::submit(request, uv_fs_mkdtemp(loop.native(), request.native(), storage.c_str(),
                                          detail::trampoline));
  }

  inline ::uv::status mkdtemp(loop &loop, request &request, std::string_view tpl, path_callback callback) {
    return mkdtemp(loop_view{loop.native()}, request, tpl, std::move(callback));
  }

  template<auto Callback>
  ::uv::status mkdtemp_static(loop_view loop, request &request, std::string_view tpl) {
    std::string storage{tpl};
    return detail::submit(request, uv_fs_mkdtemp(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_request_path_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status mkdtemp_static(loop &loop, request &request, std::string_view tpl) {
    return mkdtemp_static<Callback>(loop_view{loop.native()}, request, tpl);
  }

#if UVPP_HAS_FS_MKSTEMP
  inline ::uv::status mkstemp(loop_view loop, request &request, std::string_view tpl, temp_file_callback callback) {
    std::string storage{tpl};
    detail::set_callback(request, std::move(callback), detail::make_temp_file_result);
    return detail::submit(request, uv_fs_mkstemp(loop.native(), request.native(), storage.c_str(),
                                          detail::trampoline));
  }

  inline ::uv::status mkstemp(loop &loop, request &request, std::string_view tpl, temp_file_callback callback) {
    return mkstemp(loop_view{loop.native()}, request, tpl, std::move(callback));
  }

  template<auto Callback>
  ::uv::status mkstemp_static(loop_view loop, request &request, std::string_view tpl) {
    std::string storage{tpl};
    return detail::submit(request, uv_fs_mkstemp(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_temp_file_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status mkstemp_static(loop &loop, request &request, std::string_view tpl) {
    return mkstemp_static<Callback>(loop_view{loop.native()}, request, tpl);
  }
#endif

  inline ::uv::status rmdir(loop_view loop, request &request, std::string_view path, status_callback callback) {
    std::string storage{path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_rmdir(loop.native(), request.native(), storage.c_str(), cb);
    });
  }

  inline ::uv::status rmdir(loop &loop, request &request, std::string_view path, status_callback callback) {
    return rmdir(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status rmdir_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_rmdir(loop.native(), request.native(), storage.c_str(), cb);
    });
  }

  template<auto Callback>
  ::uv::status rmdir_static(loop &loop, request &request, std::string_view path) {
    return rmdir_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline ::uv::status fstat(loop_view loop, request &request, file_descriptor file, stat_callback callback) {
    return detail::submit_stat(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_fstat(loop.native(), request.native(), file.native(), cb);
    });
  }

  inline ::uv::status fstat(loop &loop, request &request, file_descriptor file, stat_callback callback) {
    return fstat(loop_view{loop.native()}, request, file, std::move(callback));
  }

  template<auto Callback>
  ::uv::status fstat_static(loop_view loop, request &request, file_descriptor file) {
    return detail::submit_stat_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_fstat(loop.native(), request.native(), file.native(), cb);
    });
  }

  template<auto Callback>
  ::uv::status fstat_static(loop &loop, request &request, file_descriptor file) {
    return fstat_static<Callback>(loop_view{loop.native()}, request, file);
  }

  inline ::uv::status lstat(loop_view loop, request &request, std::string_view path, stat_callback callback) {
    std::string storage{path};
    return detail::submit_stat(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_lstat(loop.native(), request.native(), storage.c_str(), cb);
    });
  }

  inline ::uv::status lstat(loop &loop, request &request, std::string_view path, stat_callback callback) {
    return lstat(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status lstat_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    return detail::submit_stat_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_lstat(loop.native(), request.native(), storage.c_str(), cb);
    });
  }

  template<auto Callback>
  ::uv::status lstat_static(loop &loop, request &request, std::string_view path) {
    return lstat_static<Callback>(loop_view{loop.native()}, request, path);
  }

#if UVPP_HAS_FS_STATFS
  inline ::uv::status statfs(loop_view loop, request &request, std::string_view path, statfs_callback callback) {
    std::string storage{path};
    return detail::submit_statfs(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_statfs(loop.native(), request.native(), storage.c_str(), cb);
    });
  }

  inline ::uv::status statfs(loop &loop, request &request, std::string_view path, statfs_callback callback) {
    return statfs(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status statfs_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    return detail::submit_statfs_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_statfs(loop.native(), request.native(), storage.c_str(), cb);
    });
  }

  template<auto Callback>
  ::uv::status statfs_static(loop &loop, request &request, std::string_view path) {
    return statfs_static<Callback>(loop_view{loop.native()}, request, path);
  }
#endif

  inline ::uv::status access(loop_view loop, request &request, std::string_view path, int mode,
                     status_callback callback) {
    std::string storage{path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_access(loop.native(), request.native(), storage.c_str(), mode, cb);
    });
  }

  inline ::uv::status access(loop &loop, request &request, std::string_view path, int mode,
                     status_callback callback) {
    return access(loop_view{loop.native()}, request, path, mode, std::move(callback));
  }

  template<auto Callback>
  ::uv::status access_static(loop_view loop, request &request, std::string_view path, int mode) {
    std::string storage{path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_access(loop.native(), request.native(), storage.c_str(), mode, cb);
    });
  }

  template<auto Callback>
  ::uv::status access_static(loop &loop, request &request, std::string_view path, int mode) {
    return access_static<Callback>(loop_view{loop.native()}, request, path, mode);
  }

  inline ::uv::status chmod(loop_view loop, request &request, std::string_view path, int mode,
                    status_callback callback) {
    std::string storage{path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_chmod(loop.native(), request.native(), storage.c_str(), mode, cb);
    });
  }

  inline ::uv::status chmod(loop &loop, request &request, std::string_view path, int mode,
                    status_callback callback) {
    return chmod(loop_view{loop.native()}, request, path, mode, std::move(callback));
  }

  template<auto Callback>
  ::uv::status chmod_static(loop_view loop, request &request, std::string_view path, int mode) {
    std::string storage{path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_chmod(loop.native(), request.native(), storage.c_str(), mode, cb);
    });
  }

  template<auto Callback>
  ::uv::status chmod_static(loop &loop, request &request, std::string_view path, int mode) {
    return chmod_static<Callback>(loop_view{loop.native()}, request, path, mode);
  }

  inline ::uv::status fchmod(loop_view loop, request &request, file_descriptor file, int mode,
                     status_callback callback) {
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_fchmod(loop.native(), request.native(), file.native(), mode, cb);
    });
  }

  inline ::uv::status fchmod(loop &loop, request &request, file_descriptor file, int mode,
                     status_callback callback) {
    return fchmod(loop_view{loop.native()}, request, file, mode, std::move(callback));
  }

  template<auto Callback>
  ::uv::status fchmod_static(loop_view loop, request &request, file_descriptor file, int mode) {
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_fchmod(loop.native(), request.native(), file.native(), mode, cb);
    });
  }

  template<auto Callback>
  ::uv::status fchmod_static(loop &loop, request &request, file_descriptor file, int mode) {
    return fchmod_static<Callback>(loop_view{loop.native()}, request, file, mode);
  }

  inline ::uv::status chown(loop_view loop, request &request, std::string_view path, uv_uid_t uid, uv_gid_t gid,
                    status_callback callback) {
    std::string storage{path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_chown(loop.native(), request.native(), storage.c_str(), uid, gid, cb);
    });
  }

  inline ::uv::status chown(loop &loop, request &request, std::string_view path, uv_uid_t uid, uv_gid_t gid,
                    status_callback callback) {
    return chown(loop_view{loop.native()}, request, path, uid, gid, std::move(callback));
  }

  template<auto Callback>
  ::uv::status chown_static(loop_view loop, request &request, std::string_view path, uv_uid_t uid, uv_gid_t gid) {
    std::string storage{path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_chown(loop.native(), request.native(), storage.c_str(), uid, gid, cb);
    });
  }

  template<auto Callback>
  ::uv::status chown_static(loop &loop, request &request, std::string_view path, uv_uid_t uid, uv_gid_t gid) {
    return chown_static<Callback>(loop_view{loop.native()}, request, path, uid, gid);
  }

  inline ::uv::status fchown(loop_view loop, request &request, file_descriptor file, uv_uid_t uid, uv_gid_t gid,
                     status_callback callback) {
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_fchown(loop.native(), request.native(), file.native(), uid, gid, cb);
    });
  }

  inline ::uv::status fchown(loop &loop, request &request, file_descriptor file, uv_uid_t uid, uv_gid_t gid,
                     status_callback callback) {
    return fchown(loop_view{loop.native()}, request, file, uid, gid, std::move(callback));
  }

  template<auto Callback>
  ::uv::status fchown_static(loop_view loop, request &request, file_descriptor file, uv_uid_t uid, uv_gid_t gid) {
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_fchown(loop.native(), request.native(), file.native(), uid, gid, cb);
    });
  }

  template<auto Callback>
  ::uv::status fchown_static(loop &loop, request &request, file_descriptor file, uv_uid_t uid, uv_gid_t gid) {
    return fchown_static<Callback>(loop_view{loop.native()}, request, file, uid, gid);
  }

  inline ::uv::status lchown(loop_view loop, request &request, std::string_view path, uv_uid_t uid, uv_gid_t gid,
                     status_callback callback) {
    std::string storage{path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_lchown(loop.native(), request.native(), storage.c_str(), uid, gid, cb);
    });
  }

  inline ::uv::status lchown(loop &loop, request &request, std::string_view path, uv_uid_t uid, uv_gid_t gid,
                     status_callback callback) {
    return lchown(loop_view{loop.native()}, request, path, uid, gid, std::move(callback));
  }

  template<auto Callback>
  ::uv::status lchown_static(loop_view loop, request &request, std::string_view path, uv_uid_t uid, uv_gid_t gid) {
    std::string storage{path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_lchown(loop.native(), request.native(), storage.c_str(), uid, gid, cb);
    });
  }

  template<auto Callback>
  ::uv::status lchown_static(loop &loop, request &request, std::string_view path, uv_uid_t uid, uv_gid_t gid) {
    return lchown_static<Callback>(loop_view{loop.native()}, request, path, uid, gid);
  }

  inline ::uv::status utime(loop_view loop, request &request, std::string_view path, double atime, double mtime,
                    status_callback callback) {
    std::string storage{path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_utime(loop.native(), request.native(), storage.c_str(), atime, mtime, cb);
    });
  }

  inline ::uv::status utime(loop &loop, request &request, std::string_view path, double atime, double mtime,
                    status_callback callback) {
    return utime(loop_view{loop.native()}, request, path, atime, mtime, std::move(callback));
  }

  template<auto Callback>
  ::uv::status utime_static(loop_view loop, request &request, std::string_view path, double atime, double mtime) {
    std::string storage{path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_utime(loop.native(), request.native(), storage.c_str(), atime, mtime, cb);
    });
  }

  template<auto Callback>
  ::uv::status utime_static(loop &loop, request &request, std::string_view path, double atime, double mtime) {
    return utime_static<Callback>(loop_view{loop.native()}, request, path, atime, mtime);
  }

  inline ::uv::status futime(loop_view loop, request &request, file_descriptor file, double atime, double mtime,
                     status_callback callback) {
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_futime(loop.native(), request.native(), file.native(), atime, mtime, cb);
    });
  }

  inline ::uv::status futime(loop &loop, request &request, file_descriptor file, double atime, double mtime,
                     status_callback callback) {
    return futime(loop_view{loop.native()}, request, file, atime, mtime, std::move(callback));
  }

  template<auto Callback>
  ::uv::status futime_static(loop_view loop, request &request, file_descriptor file, double atime, double mtime) {
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_futime(loop.native(), request.native(), file.native(), atime, mtime, cb);
    });
  }

  template<auto Callback>
  ::uv::status futime_static(loop &loop, request &request, file_descriptor file, double atime, double mtime) {
    return futime_static<Callback>(loop_view{loop.native()}, request, file, atime, mtime);
  }

  inline ::uv::status lutime(loop_view loop, request &request, std::string_view path, double atime, double mtime,
                     status_callback callback) {
    std::string storage{path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_lutime(loop.native(), request.native(), storage.c_str(), atime, mtime, cb);
    });
  }

  inline ::uv::status lutime(loop &loop, request &request, std::string_view path, double atime, double mtime,
                     status_callback callback) {
    return lutime(loop_view{loop.native()}, request, path, atime, mtime, std::move(callback));
  }

  template<auto Callback>
  ::uv::status lutime_static(loop_view loop, request &request, std::string_view path, double atime, double mtime) {
    std::string storage{path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_lutime(loop.native(), request.native(), storage.c_str(), atime, mtime, cb);
    });
  }

  template<auto Callback>
  ::uv::status lutime_static(loop &loop, request &request, std::string_view path, double atime, double mtime) {
    return lutime_static<Callback>(loop_view{loop.native()}, request, path, atime, mtime);
  }

  inline ::uv::status fsync(loop_view loop, request &request, file_descriptor file, status_callback callback) {
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_fsync(loop.native(), request.native(), file.native(), cb);
    });
  }

  inline ::uv::status fsync(loop &loop, request &request, file_descriptor file, status_callback callback) {
    return fsync(loop_view{loop.native()}, request, file, std::move(callback));
  }

  template<auto Callback>
  ::uv::status fsync_static(loop_view loop, request &request, file_descriptor file) {
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_fsync(loop.native(), request.native(), file.native(), cb);
    });
  }

  template<auto Callback>
  ::uv::status fsync_static(loop &loop, request &request, file_descriptor file) {
    return fsync_static<Callback>(loop_view{loop.native()}, request, file);
  }

  inline ::uv::status fdatasync(loop_view loop, request &request, file_descriptor file, status_callback callback) {
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_fdatasync(loop.native(), request.native(), file.native(), cb);
    });
  }

  inline ::uv::status fdatasync(loop &loop, request &request, file_descriptor file, status_callback callback) {
    return fdatasync(loop_view{loop.native()}, request, file, std::move(callback));
  }

  template<auto Callback>
  ::uv::status fdatasync_static(loop_view loop, request &request, file_descriptor file) {
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_fdatasync(loop.native(), request.native(), file.native(), cb);
    });
  }

  template<auto Callback>
  ::uv::status fdatasync_static(loop &loop, request &request, file_descriptor file) {
    return fdatasync_static<Callback>(loop_view{loop.native()}, request, file);
  }

  inline ::uv::status ftruncate(loop_view loop, request &request, file_descriptor file, int64_t offset,
                        status_callback callback) {
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_ftruncate(loop.native(), request.native(), file.native(), offset, cb);
    });
  }

  inline ::uv::status ftruncate(loop &loop, request &request, file_descriptor file, int64_t offset,
                        status_callback callback) {
    return ftruncate(loop_view{loop.native()}, request, file, offset, std::move(callback));
  }

  template<auto Callback>
  ::uv::status ftruncate_static(loop_view loop, request &request, file_descriptor file, int64_t offset) {
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_ftruncate(loop.native(), request.native(), file.native(), offset, cb);
    });
  }

  template<auto Callback>
  ::uv::status ftruncate_static(loop &loop, request &request, file_descriptor file, int64_t offset) {
    return ftruncate_static<Callback>(loop_view{loop.native()}, request, file, offset);
  }

  inline ::uv::status link(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                   status_callback callback) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_link(loop.native(), request.native(), path_storage.c_str(), new_path_storage.c_str(), cb);
    });
  }

  inline ::uv::status link(loop &loop, request &request, std::string_view path, std::string_view new_path,
                   status_callback callback) {
    return link(loop_view{loop.native()}, request, path, new_path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status link_static(loop_view loop, request &request, std::string_view path, std::string_view new_path) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_link(loop.native(), request.native(), path_storage.c_str(), new_path_storage.c_str(), cb);
    });
  }

  template<auto Callback>
  ::uv::status link_static(loop &loop, request &request, std::string_view path, std::string_view new_path) {
    return link_static<Callback>(loop_view{loop.native()}, request, path, new_path);
  }

  inline ::uv::status symlink(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                      int flags, status_callback callback) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    return detail::submit_status(request, std::move(callback), [&](uv_fs_cb cb) {
      return uv_fs_symlink(loop.native(), request.native(), path_storage.c_str(), new_path_storage.c_str(),
                           flags, cb);
    });
  }

  inline ::uv::status symlink(loop &loop, request &request, std::string_view path, std::string_view new_path,
                      int flags, status_callback callback) {
    return symlink(loop_view{loop.native()}, request, path, new_path, flags, std::move(callback));
  }

  inline ::uv::status symlink(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                      status_callback callback) {
    return symlink(loop, request, path, new_path, 0, std::move(callback));
  }

  inline ::uv::status symlink(loop &loop, request &request, std::string_view path, std::string_view new_path,
                      status_callback callback) {
    return symlink(loop_view{loop.native()}, request, path, new_path, std::move(callback));
  }

  inline ::uv::status symlink(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                      symlink_flag flag, status_callback callback) {
    return symlink(loop, request, path, new_path, static_cast<int>(flag), std::move(callback));
  }

  inline ::uv::status symlink(loop &loop, request &request, std::string_view path, std::string_view new_path,
                      symlink_flag flag, status_callback callback) {
    return symlink(loop_view{loop.native()}, request, path, new_path, flag, std::move(callback));
  }

  template<auto Callback>
  ::uv::status symlink_static(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                      int flags = 0) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    return detail::submit_status_static<Callback>(request, [&](uv_fs_cb cb) {
      return uv_fs_symlink(loop.native(), request.native(), path_storage.c_str(), new_path_storage.c_str(),
                           flags, cb);
    });
  }

  template<auto Callback>
  ::uv::status symlink_static(loop &loop, request &request, std::string_view path, std::string_view new_path,
                      int flags = 0) {
    return symlink_static<Callback>(loop_view{loop.native()}, request, path, new_path, flags);
  }

  inline ::uv::status realpath(loop_view loop, request &request, std::string_view path, path_callback callback) {
    std::string storage{path};
    detail::set_callback(request, std::move(callback), detail::make_path_result);
    return detail::submit(request, uv_fs_realpath(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline ::uv::status realpath(loop &loop, request &request, std::string_view path, path_callback callback) {
    return realpath(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status realpath_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    return detail::submit(request, uv_fs_realpath(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_path_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status realpath_static(loop &loop, request &request, std::string_view path) {
    return realpath_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline ::uv::status readlink(loop_view loop, request &request, std::string_view path, path_callback callback) {
    std::string storage{path};
    detail::set_callback(request, std::move(callback), detail::make_path_result);
    return detail::submit(request, uv_fs_readlink(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline ::uv::status readlink(loop &loop, request &request, std::string_view path, path_callback callback) {
    return readlink(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status readlink_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    return detail::submit(request, uv_fs_readlink(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_path_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status readlink_static(loop &loop, request &request, std::string_view path) {
    return readlink_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline ::uv::status copyfile(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                       int flags, status_callback callback) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    detail::set_callback(request, std::move(callback), detail::make_status_result);
    return detail::submit(request, uv_fs_copyfile(loop.native(), request.native(), path_storage.c_str(),
                                           new_path_storage.c_str(), flags, detail::trampoline));
  }

  inline ::uv::status copyfile(loop &loop, request &request, std::string_view path, std::string_view new_path,
                       int flags, status_callback callback) {
    return copyfile(loop_view{loop.native()}, request, path, new_path, flags, std::move(callback));
  }

  inline ::uv::status copyfile(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                       copyfile_flag flag, status_callback callback) {
    return copyfile(loop, request, path, new_path, static_cast<int>(flag), std::move(callback));
  }

  inline ::uv::status copyfile(loop &loop, request &request, std::string_view path, std::string_view new_path,
                       copyfile_flag flag, status_callback callback) {
    return copyfile(loop_view{loop.native()}, request, path, new_path, flag, std::move(callback));
  }

  template<auto Callback>
  ::uv::status copyfile_static(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                       int flags = 0) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    return detail::submit(request, uv_fs_copyfile(loop.native(), request.native(), path_storage.c_str(),
                                           new_path_storage.c_str(), flags,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_status_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status copyfile_static(loop &loop, request &request, std::string_view path, std::string_view new_path,
                       int flags = 0) {
    return copyfile_static<Callback>(loop_view{loop.native()}, request, path, new_path, flags);
  }

  inline ::uv::status sendfile(loop_view loop, request &request, file_descriptor out_file, file_descriptor in_file,
                       int64_t in_offset, std::size_t length, byte_count_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    return detail::submit(request, uv_fs_sendfile(loop.native(), request.native(), out_file.native(), in_file.native(),
                                           in_offset, length, detail::trampoline));
  }

  inline ::uv::status sendfile(loop &loop, request &request, file_descriptor out_file, file_descriptor in_file,
                       int64_t in_offset, std::size_t length, byte_count_callback callback) {
    return sendfile(loop_view{loop.native()}, request, out_file, in_file, in_offset, length, std::move(callback));
  }

  template<auto Callback>
  ::uv::status sendfile_static(loop_view loop, request &request, file_descriptor out_file, file_descriptor in_file,
                       int64_t in_offset, std::size_t length) {
    return detail::submit(request, uv_fs_sendfile(loop.native(), request.native(), out_file.native(), in_file.native(),
                                           in_offset, length,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_byte_count_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status sendfile_static(loop &loop, request &request, file_descriptor out_file, file_descriptor in_file,
                       int64_t in_offset, std::size_t length) {
    return sendfile_static<Callback>(loop_view{loop.native()}, request, out_file, in_file, in_offset, length);
  }

  inline ::uv::status scandir(loop_view loop, request &request, std::string_view path, int flags,
                      scandir_callback callback) {
    std::string storage{path};
    detail::set_callback(request, std::move(callback), detail::make_scandir_result);
    return detail::submit(request, uv_fs_scandir(loop.native(), request.native(), storage.c_str(), flags,
                                          detail::trampoline));
  }

  inline ::uv::status scandir(loop &loop, request &request, std::string_view path, int flags,
                      scandir_callback callback) {
    return scandir(loop_view{loop.native()}, request, path, flags, std::move(callback));
  }

  template<auto Callback>
  ::uv::status scandir_static(loop_view loop, request &request, std::string_view path, int flags = 0) {
    std::string storage{path};
    return detail::submit(request, uv_fs_scandir(loop.native(), request.native(), storage.c_str(), flags,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_scandir_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status scandir_static(loop &loop, request &request, std::string_view path, int flags = 0) {
    return scandir_static<Callback>(loop_view{loop.native()}, request, path, flags);
  }

  inline ::uv::status opendir(loop_view loop, request &request, std::string_view path, opendir_callback callback) {
    std::string storage{path};
    detail::set_callback(request, std::move(callback), detail::make_opendir_result);
    return detail::submit(request, uv_fs_opendir(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline ::uv::status opendir(loop &loop, request &request, std::string_view path, opendir_callback callback) {
    return opendir(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  ::uv::status opendir_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    return detail::submit(request, uv_fs_opendir(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_opendir_result(req);
        uv::detail::invoke_static_callback<Callback>(req, std::move(result));
      }));
  }

  template<auto Callback>
  ::uv::status opendir_static(loop &loop, request &request, std::string_view path) {
    return opendir_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline ::uv::status readdir(loop_view loop, request &request, directory &dir, directory_read_buffer &buffer,
                      readdir_callback callback) {
    buffer.prepare(dir);
    detail::set_callback(request, std::move(callback), detail::make_readdir_result);
    return detail::submit(request, uv_fs_readdir(loop.native(), request.native(), dir.native(), detail::trampoline));
  }

  inline ::uv::status readdir(loop &loop, request &request, directory &dir, directory_read_buffer &buffer,
                      readdir_callback callback) {
    return readdir(loop_view{loop.native()}, request, dir, buffer, std::move(callback));
  }

  template<auto Callback>
  ::uv::status readdir_static(loop_view loop, request &request, directory &dir, directory_read_buffer &buffer) {
    buffer.prepare(dir);
    return detail::submit(request, uv_fs_readdir(loop.native(), request.native(), dir.native(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_readdir_result(req);
        uv::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  ::uv::status readdir_static(loop &loop, request &request, directory &dir, directory_read_buffer &buffer) {
    return readdir_static<Callback>(loop_view{loop.native()}, request, dir, buffer);
  }

  inline ::uv::status closedir(loop_view loop, request &request, directory &&dir, status_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_status_result);
    return detail::submit_closedir(dir, [&](uv_dir_t *native_dir) {
      return detail::submit(request, uv_fs_closedir(loop.native(), request.native(), native_dir, detail::trampoline));
    });
  }

  inline ::uv::status closedir(loop &loop, request &request, directory &&dir, status_callback callback) {
    return closedir(loop_view{loop.native()}, request, std::move(dir), std::move(callback));
  }

  template<auto Callback>
  ::uv::status closedir_static(loop_view loop, request &request, directory &&dir) {
    return detail::submit_closedir(dir, [&](uv_dir_t *native_dir) {
      return detail::submit(request, uv_fs_closedir(loop.native(), request.native(), native_dir,
        [](uv_fs_t *raw) noexcept {
          auto &req = request::from_native(raw);
          auto result = detail::make_status_result(req);
          uv::detail::invoke_static_callback<Callback>(req, result);
        }));
    });
  }

  template<auto Callback>
  ::uv::status closedir_static(loop &loop, request &request, directory &&dir) {
    return closedir_static<Callback>(loop_view{loop.native()}, request, std::move(dir));
  }

}

namespace uv::fs {

  using ::uv::raw::fs::copyfile_flag;
  using ::uv::raw::fs::directory_entry_type;
  using ::uv::raw::fs::symlink_flag;

  class status_result {
  public:
    status_result() = default;
    explicit status_result(ssize_t result) noexcept : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }

  private:
    ssize_t result_ = 0;
  };

  class open_result {
  public:
    explicit open_result(ssize_t result) noexcept : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }

    file_descriptor file() const noexcept {
      return ok() ? file_descriptor{static_cast<uv_file>(result_)} : file_descriptor{};
    }

  private:
    ssize_t result_ = 0;
  };

  class byte_count_result {
  public:
    explicit byte_count_result(ssize_t result) noexcept : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }

    std::size_t count() const noexcept {
      return ok() ? static_cast<std::size_t>(result_) : 0;
    }

  private:
    ssize_t result_ = 0;
  };

  class read_result {
  public:
    read_result(ssize_t result, owned_buffer buffer) noexcept
      : result_{result}, buffer_{std::move(buffer)} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }

    std::size_t count() const noexcept {
      return ok() ? static_cast<std::size_t>(result_) : 0;
    }

    std::span<const std::byte> bytes() const noexcept {
      return buffer_.bytes().first(count());
    }

    const owned_buffer &buffer() const noexcept {
      return buffer_;
    }

    owned_buffer take_buffer() noexcept {
      return std::move(buffer_);
    }

  private:
    ssize_t result_ = 0;
    owned_buffer buffer_;
  };

  class stat_result {
  public:
    stat_result(ssize_t result, const uv_stat_t *stat) noexcept : result_{result} {
      if (result_ >= 0 && stat) {
        stat_ = *stat;
        file_status_ = ::uv::fs::file_status{*stat};
      }
    }

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }
    const uv_stat_t &native() const noexcept { return stat_; }
    const ::uv::fs::file_status &file_status() const noexcept { return file_status_; }

  private:
    ssize_t result_ = 0;
    uv_stat_t stat_{};
    ::uv::fs::file_status file_status_{};
  };

  class path_result {
  public:
    path_result(ssize_t result, std::string path) : result_{result}, path_{std::move(path)} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }
    std::string_view path() const noexcept { return path_; }
    std::string take_path() { return std::move(path_); }

  private:
    ssize_t result_ = 0;
    std::string path_;
  };

  class temp_file_result {
  public:
    temp_file_result(ssize_t result, std::string path) : result_{result}, path_{std::move(path)} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }

    file_descriptor file() const noexcept {
      return ok() ? file_descriptor{static_cast<uv_file>(result_)} : file_descriptor{};
    }

    std::string_view path() const noexcept { return path_; }
    std::string take_path() { return std::move(path_); }

  private:
    ssize_t result_ = 0;
    std::string path_;
  };

  class statfs_result {
  public:
    statfs_result(ssize_t result, const uv_statfs_t *statfs) noexcept : result_{result} {
      if (statfs) {
        statfs_ = *statfs;
      }
    }

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }
    const uv_statfs_t &native() const noexcept { return statfs_; }

  private:
    ssize_t result_ = 0;
    uv_statfs_t statfs_{};
  };

  struct directory_entry {
    std::string name;
    directory_entry_type type = directory_entry_type::unknown;
  };

  class scandir_result {
  public:
    scandir_result(ssize_t result, std::vector<directory_entry> entries)
      : result_{result}, entries_{std::move(entries)} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    ::uv::status status() const noexcept { return ::uv::status::from_native(raw_status()); }
    int raw_status() const noexcept { return result_ < 0 ? static_cast<int>(result_) : 0; }
    uv::error_code error_code() const noexcept { return make_error_code(raw_status()); }

    std::span<const directory_entry> entries() const noexcept {
      return entries_;
    }

    auto begin() const noexcept { return entries_.begin(); }
    auto end()   const noexcept { return entries_.end(); }

    std::vector<directory_entry> take_entries() {
      return std::move(entries_);
    }

  private:
    ssize_t result_ = 0;
    std::vector<directory_entry> entries_;
  };

  using open_callback = std::function<void(open_result)>;
  using status_callback = std::function<void(status_result)>;
  using byte_count_callback = std::function<void(byte_count_result)>;
  using owned_read_callback = std::function<void(read_result)>;
  // Compatibility spelling for the result type returned by read_owned().
  using read_callback = owned_read_callback;
  using stat_callback = std::function<void(stat_result)>;
  using path_callback = std::function<void(path_result)>;
  using temp_file_callback = std::function<void(temp_file_result)>;
  using statfs_callback = std::function<void(statfs_result)>;
  using scandir_callback = std::function<void(scandir_result)>;

  namespace detail {

    template<class Callback>
    struct callback_state {
      ::uv::raw::fs::request request;
      Callback callback;
    };

    template<class State, class Submit>
    void submit_owned(State *state, Submit submit) {
      try {
        auto submitted = submit();
        if (!submitted) {
          state->request.cleanup();
          submitted.throw_if_error();
        }
      } catch (...) {
        delete state;
        throw;
      }
    }

    inline void finish_status(callback_state<status_callback> *state, ::uv::raw::fs::request &request,
                              ::uv::raw::fs::status_result result) {
      auto cleanup = request.scoped_cleanup();
      auto callback = std::move(state->callback);
      auto out = status_result{result.raw()};
      cleanup.cleanup();
      delete state;
      callback(out);
    }

    inline void finish_byte_count(callback_state<byte_count_callback> *state, ::uv::raw::fs::request &request,
                                  ::uv::raw::fs::byte_count_result result) {
      auto cleanup = request.scoped_cleanup();
      auto callback = std::move(state->callback);
      auto out = byte_count_result{result.raw()};
      cleanup.cleanup();
      delete state;
      callback(out);
    }

    inline void finish_stat(callback_state<stat_callback> *state, ::uv::raw::fs::request &request,
                            ::uv::raw::fs::stat_result result) {
      auto cleanup = request.scoped_cleanup();
      auto callback = std::move(state->callback);
      auto out = stat_result{result.raw(), result.native_ptr()};
      cleanup.cleanup();
      delete state;
      callback(out);
    }

    inline void finish_statfs(callback_state<statfs_callback> *state, ::uv::raw::fs::request &request,
                              ::uv::raw::fs::statfs_result result) {
      auto cleanup = request.scoped_cleanup();
      auto callback = std::move(state->callback);
      auto out = statfs_result{result.raw(), result.native_ptr()};
      cleanup.cleanup();
      delete state;
      callback(out);
    }

    template<class Submit>
    void submit_status_operation(status_callback callback, Submit submit) {
      auto *state = new callback_state<status_callback>{{}, std::move(callback)};
      submit_owned(state, [&] {
        return submit(state->request, [state](::uv::raw::fs::request &request, ::uv::raw::fs::status_result result) {
          finish_status(state, request, result);
        });
      });
    }

    template<class Submit>
    void submit_stat_operation(stat_callback callback, Submit submit) {
      auto *state = new callback_state<stat_callback>{{}, std::move(callback)};
      submit_owned(state, [&] {
        return submit(state->request, [state](::uv::raw::fs::request &request, ::uv::raw::fs::stat_result result) {
          finish_stat(state, request, result);
        });
      });
    }

    template<class Submit>
    void submit_statfs_operation(statfs_callback callback, Submit submit) {
      auto *state = new callback_state<statfs_callback>{{}, std::move(callback)};
      submit_owned(state, [&] {
        return submit(state->request, [state](::uv::raw::fs::request &request, ::uv::raw::fs::statfs_result result) {
          finish_statfs(state, request, result);
        });
      });
    }

  }

  inline void open(loop_view loop, std::string_view path, int flags, int mode, open_callback callback) {
    auto *state = new detail::callback_state<open_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::open(loop, state->request, path, flags, mode, [state](::uv::raw::fs::request &request, ::uv::raw::fs::open_result result) {
        auto cleanup = request.scoped_cleanup();
        auto callback = std::move(state->callback);
        auto out = open_result{result.raw()};
        cleanup.cleanup();
        delete state;
        callback(out);
      });
    });
  }

  inline void open(loop &loop, std::string_view path, int flags, int mode, open_callback callback) {
    open(loop_view{loop.native()}, path, flags, mode, std::move(callback));
  }

  inline void close(loop_view loop, file_descriptor file, status_callback callback) {
    auto *state = new detail::callback_state<status_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::close(loop, state->request, file, [state](::uv::raw::fs::request &request, ::uv::raw::fs::status_result result) {
        detail::finish_status(state, request, result);
      });
    });
  }

  inline void close(loop &loop, file_descriptor file, status_callback callback) {
    close(loop_view{loop.native()}, file, std::move(callback));
  }

  inline void read(loop_view loop, file_descriptor file, std::span<std::byte> bytes, int64_t offset,
                   byte_count_callback callback) {
    auto *state = new detail::callback_state<byte_count_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::read(loop, state->request, file, bytes, offset,
        [state](::uv::raw::fs::request &request, ::uv::raw::fs::byte_count_result result) {
          detail::finish_byte_count(state, request, result);
        });
    });
  }

  inline void read(loop &loop, file_descriptor file, std::span<std::byte> bytes, int64_t offset,
                   byte_count_callback callback) {
    read(loop_view{loop.native()}, file, bytes, offset, std::move(callback));
  }

  inline void read_owned(loop_view loop, file_descriptor file, std::size_t size, int64_t offset,
                         owned_read_callback callback) {
    struct state_type {
      ::uv::raw::fs::request request;
      owned_read_callback callback;
      owned_buffer buffer;
    };

    auto *state = new state_type{{}, std::move(callback), owned_buffer{size}};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::read(loop, state->request, file, state->buffer.bytes(), offset,
        [state](::uv::raw::fs::request &request, ::uv::raw::fs::byte_count_result result) {
          auto cleanup = request.scoped_cleanup();
          auto callback = std::move(state->callback);
          auto out = read_result{result.raw(), std::move(state->buffer)};
          cleanup.cleanup();
          delete state;
          callback(std::move(out));
        });
    });
  }

  inline void read_owned(loop &loop, file_descriptor file, std::size_t size, int64_t offset,
                         owned_read_callback callback) {
    read_owned(loop_view{loop.native()}, file, size, offset, std::move(callback));
  }

  inline void write(loop_view loop, file_descriptor file, std::span<const std::byte> bytes, int64_t offset,
                    byte_count_callback callback) {
    auto *state = new detail::callback_state<byte_count_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::write(loop, state->request, file, bytes, offset,
        [state](::uv::raw::fs::request &request, ::uv::raw::fs::byte_count_result result) {
          detail::finish_byte_count(state, request, result);
        });
    });
  }

  inline void write(loop &loop, file_descriptor file, std::span<const std::byte> bytes, int64_t offset,
                    byte_count_callback callback) {
    write(loop_view{loop.native()}, file, bytes, offset, std::move(callback));
  }

  inline void write_copy(loop_view loop, file_descriptor file, std::span<const std::byte> bytes, int64_t offset,
                         byte_count_callback callback) {
    struct state_type {
      ::uv::raw::fs::request request;
      byte_count_callback callback;
      owned_buffer buffer;
    };

    auto *state = new state_type{{}, std::move(callback), owned_buffer{bytes}};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::write(loop, state->request, file, state->buffer.bytes(), offset,
        [state](::uv::raw::fs::request &request, ::uv::raw::fs::byte_count_result result) {
          auto cleanup = request.scoped_cleanup();
          auto callback = std::move(state->callback);
          auto out = byte_count_result{result.raw()};
          cleanup.cleanup();
          delete state;
          callback(out);
        });
    });
  }

  inline void write_copy(loop &loop, file_descriptor file, std::span<const std::byte> bytes, int64_t offset,
                         byte_count_callback callback) {
    write_copy(loop_view{loop.native()}, file, bytes, offset, std::move(callback));
  }

  inline void stat(loop_view loop, std::string_view path, stat_callback callback) {
    detail::submit_stat_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::stat_callback cb) {
      return ::uv::raw::fs::stat(loop, request, path, std::move(cb));
    });
  }

  inline void stat(loop &loop, std::string_view path, stat_callback callback) {
    stat(loop_view{loop.native()}, path, std::move(callback));
  }

  inline void unlink(loop_view loop, std::string_view path, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::unlink(loop, request, path, std::move(cb));
    });
  }

  inline void unlink(loop &loop, std::string_view path, status_callback callback) {
    unlink(loop_view{loop.native()}, path, std::move(callback));
  }

  inline void rename(loop_view loop, std::string_view path, std::string_view new_path,
                     status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::rename(loop, request, path, new_path, std::move(cb));
    });
  }

  inline void rename(loop &loop, std::string_view path, std::string_view new_path,
                     status_callback callback) {
    rename(loop_view{loop.native()}, path, new_path, std::move(callback));
  }

  inline void mkdir(loop_view loop, std::string_view path, int mode, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::mkdir(loop, request, path, mode, std::move(cb));
    });
  }

  inline void mkdir(loop &loop, std::string_view path, int mode, status_callback callback) {
    mkdir(loop_view{loop.native()}, path, mode, std::move(callback));
  }

  inline void mkdtemp(loop_view loop, std::string_view tpl, path_callback callback) {
    auto *state = new detail::callback_state<path_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::mkdtemp(loop, state->request, tpl, [state](::uv::raw::fs::request &request, ::uv::raw::fs::path_result result) {
        auto cleanup = request.scoped_cleanup();
        auto callback = std::move(state->callback);
        auto out = path_result{result.raw(), std::string{result.path()}};
        cleanup.cleanup();
        delete state;
        callback(std::move(out));
      });
    });
  }

  inline void mkdtemp(loop &loop, std::string_view tpl, path_callback callback) {
    mkdtemp(loop_view{loop.native()}, tpl, std::move(callback));
  }

#if UVPP_HAS_FS_MKSTEMP
  inline void mkstemp(loop_view loop, std::string_view tpl, temp_file_callback callback) {
    auto *state = new detail::callback_state<temp_file_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::mkstemp(loop, state->request, tpl,
        [state](::uv::raw::fs::request &request, ::uv::raw::fs::temp_file_result result) {
          auto cleanup = request.scoped_cleanup();
          auto callback = std::move(state->callback);
          auto out = temp_file_result{result.raw(), std::string{result.path()}};
          cleanup.cleanup();
          delete state;
          callback(std::move(out));
        });
    });
  }

  inline void mkstemp(loop &loop, std::string_view tpl, temp_file_callback callback) {
    mkstemp(loop_view{loop.native()}, tpl, std::move(callback));
  }
#endif

  inline void rmdir(loop_view loop, std::string_view path, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::rmdir(loop, request, path, std::move(cb));
    });
  }

  inline void rmdir(loop &loop, std::string_view path, status_callback callback) {
    rmdir(loop_view{loop.native()}, path, std::move(callback));
  }

  inline void fstat(loop_view loop, file_descriptor file, stat_callback callback) {
    detail::submit_stat_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::stat_callback cb) {
      return ::uv::raw::fs::fstat(loop, request, file, std::move(cb));
    });
  }

  inline void fstat(loop &loop, file_descriptor file, stat_callback callback) {
    fstat(loop_view{loop.native()}, file, std::move(callback));
  }

  inline void lstat(loop_view loop, std::string_view path, stat_callback callback) {
    detail::submit_stat_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::stat_callback cb) {
      return ::uv::raw::fs::lstat(loop, request, path, std::move(cb));
    });
  }

  inline void lstat(loop &loop, std::string_view path, stat_callback callback) {
    lstat(loop_view{loop.native()}, path, std::move(callback));
  }

#if UVPP_HAS_FS_STATFS
  inline void statfs(loop_view loop, std::string_view path, statfs_callback callback) {
    detail::submit_statfs_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::statfs_callback cb) {
      return ::uv::raw::fs::statfs(loop, request, path, std::move(cb));
    });
  }

  inline void statfs(loop &loop, std::string_view path, statfs_callback callback) {
    statfs(loop_view{loop.native()}, path, std::move(callback));
  }
#endif

  inline void access(loop_view loop, std::string_view path, int mode, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::access(loop, request, path, mode, std::move(cb));
    });
  }

  inline void access(loop &loop, std::string_view path, int mode, status_callback callback) {
    access(loop_view{loop.native()}, path, mode, std::move(callback));
  }

  inline void chmod(loop_view loop, std::string_view path, int mode, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::chmod(loop, request, path, mode, std::move(cb));
    });
  }

  inline void chmod(loop &loop, std::string_view path, int mode, status_callback callback) {
    chmod(loop_view{loop.native()}, path, mode, std::move(callback));
  }

  inline void fchmod(loop_view loop, file_descriptor file, int mode, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::fchmod(loop, request, file, mode, std::move(cb));
    });
  }

  inline void fchmod(loop &loop, file_descriptor file, int mode, status_callback callback) {
    fchmod(loop_view{loop.native()}, file, mode, std::move(callback));
  }

  inline void chown(loop_view loop, std::string_view path, uv_uid_t uid, uv_gid_t gid,
                    status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::chown(loop, request, path, uid, gid, std::move(cb));
    });
  }

  inline void chown(loop &loop, std::string_view path, uv_uid_t uid, uv_gid_t gid,
                    status_callback callback) {
    chown(loop_view{loop.native()}, path, uid, gid, std::move(callback));
  }

  inline void fchown(loop_view loop, file_descriptor file, uv_uid_t uid, uv_gid_t gid,
                     status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::fchown(loop, request, file, uid, gid, std::move(cb));
    });
  }

  inline void fchown(loop &loop, file_descriptor file, uv_uid_t uid, uv_gid_t gid,
                     status_callback callback) {
    fchown(loop_view{loop.native()}, file, uid, gid, std::move(callback));
  }

  inline void lchown(loop_view loop, std::string_view path, uv_uid_t uid, uv_gid_t gid,
                     status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::lchown(loop, request, path, uid, gid, std::move(cb));
    });
  }

  inline void lchown(loop &loop, std::string_view path, uv_uid_t uid, uv_gid_t gid,
                     status_callback callback) {
    lchown(loop_view{loop.native()}, path, uid, gid, std::move(callback));
  }

  inline void utime(loop_view loop, std::string_view path, double atime, double mtime,
                    status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::utime(loop, request, path, atime, mtime, std::move(cb));
    });
  }

  inline void utime(loop &loop, std::string_view path, double atime, double mtime,
                    status_callback callback) {
    utime(loop_view{loop.native()}, path, atime, mtime, std::move(callback));
  }

  inline void futime(loop_view loop, file_descriptor file, double atime, double mtime,
                     status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::futime(loop, request, file, atime, mtime, std::move(cb));
    });
  }

  inline void futime(loop &loop, file_descriptor file, double atime, double mtime,
                     status_callback callback) {
    futime(loop_view{loop.native()}, file, atime, mtime, std::move(callback));
  }

  inline void lutime(loop_view loop, std::string_view path, double atime, double mtime,
                     status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::lutime(loop, request, path, atime, mtime, std::move(cb));
    });
  }

  inline void lutime(loop &loop, std::string_view path, double atime, double mtime,
                     status_callback callback) {
    lutime(loop_view{loop.native()}, path, atime, mtime, std::move(callback));
  }

  inline void fsync(loop_view loop, file_descriptor file, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::fsync(loop, request, file, std::move(cb));
    });
  }

  inline void fsync(loop &loop, file_descriptor file, status_callback callback) {
    fsync(loop_view{loop.native()}, file, std::move(callback));
  }

  inline void fdatasync(loop_view loop, file_descriptor file, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::fdatasync(loop, request, file, std::move(cb));
    });
  }

  inline void fdatasync(loop &loop, file_descriptor file, status_callback callback) {
    fdatasync(loop_view{loop.native()}, file, std::move(callback));
  }

  inline void ftruncate(loop_view loop, file_descriptor file, int64_t offset, status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::ftruncate(loop, request, file, offset, std::move(cb));
    });
  }

  inline void ftruncate(loop &loop, file_descriptor file, int64_t offset, status_callback callback) {
    ftruncate(loop_view{loop.native()}, file, offset, std::move(callback));
  }

  inline void link(loop_view loop, std::string_view path, std::string_view new_path,
                   status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::link(loop, request, path, new_path, std::move(cb));
    });
  }

  inline void link(loop &loop, std::string_view path, std::string_view new_path,
                   status_callback callback) {
    link(loop_view{loop.native()}, path, new_path, std::move(callback));
  }

  inline void symlink(loop_view loop, std::string_view path, std::string_view new_path, int flags,
                      status_callback callback) {
    detail::submit_status_operation(std::move(callback), [&](::uv::raw::fs::request &request, ::uv::raw::fs::status_callback cb) {
      return ::uv::raw::fs::symlink(loop, request, path, new_path, flags, std::move(cb));
    });
  }

  inline void symlink(loop &loop, std::string_view path, std::string_view new_path, int flags,
                      status_callback callback) {
    symlink(loop_view{loop.native()}, path, new_path, flags, std::move(callback));
  }

  inline void symlink(loop_view loop, std::string_view path, std::string_view new_path,
                      status_callback callback) {
    symlink(loop, path, new_path, 0, std::move(callback));
  }

  inline void symlink(loop &loop, std::string_view path, std::string_view new_path,
                      status_callback callback) {
    symlink(loop_view{loop.native()}, path, new_path, std::move(callback));
  }

  inline void symlink(loop_view loop, std::string_view path, std::string_view new_path, symlink_flag flag,
                      status_callback callback) {
    symlink(loop, path, new_path, static_cast<int>(flag), std::move(callback));
  }

  inline void symlink(loop &loop, std::string_view path, std::string_view new_path, symlink_flag flag,
                      status_callback callback) {
    symlink(loop_view{loop.native()}, path, new_path, flag, std::move(callback));
  }

  inline void realpath(loop_view loop, std::string_view path, path_callback callback) {
    auto *state = new detail::callback_state<path_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::realpath(loop, state->request, path, [state](::uv::raw::fs::request &request, ::uv::raw::fs::path_result result) {
        auto cleanup = request.scoped_cleanup();
        auto callback = std::move(state->callback);
        auto out = path_result{result.raw(), std::string{result.path()}};
        cleanup.cleanup();
        delete state;
        callback(std::move(out));
      });
    });
  }

  inline void realpath(loop &loop, std::string_view path, path_callback callback) {
    realpath(loop_view{loop.native()}, path, std::move(callback));
  }

  inline void readlink(loop_view loop, std::string_view path, path_callback callback) {
    auto *state = new detail::callback_state<path_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::readlink(loop, state->request, path, [state](::uv::raw::fs::request &request, ::uv::raw::fs::path_result result) {
        auto cleanup = request.scoped_cleanup();
        auto callback = std::move(state->callback);
        auto out = path_result{result.raw(), std::string{result.path()}};
        cleanup.cleanup();
        delete state;
        callback(std::move(out));
      });
    });
  }

  inline void readlink(loop &loop, std::string_view path, path_callback callback) {
    readlink(loop_view{loop.native()}, path, std::move(callback));
  }

  inline void copyfile(loop_view loop, std::string_view path, std::string_view new_path, int flags,
                       status_callback callback) {
    auto *state = new detail::callback_state<status_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::copyfile(loop, state->request, path, new_path, flags,
        [state](::uv::raw::fs::request &request, ::uv::raw::fs::status_result result) {
          detail::finish_status(state, request, result);
        });
    });
  }

  inline void copyfile(loop &loop, std::string_view path, std::string_view new_path, int flags,
                       status_callback callback) {
    copyfile(loop_view{loop.native()}, path, new_path, flags, std::move(callback));
  }

  inline void copyfile(loop_view loop, std::string_view path, std::string_view new_path, copyfile_flag flag,
                       status_callback callback) {
    copyfile(loop, path, new_path, static_cast<int>(flag), std::move(callback));
  }

  inline void copyfile(loop &loop, std::string_view path, std::string_view new_path, copyfile_flag flag,
                       status_callback callback) {
    copyfile(loop_view{loop.native()}, path, new_path, flag, std::move(callback));
  }

  inline void sendfile(loop_view loop, file_descriptor out_file, file_descriptor in_file, int64_t in_offset,
                       std::size_t length, byte_count_callback callback) {
    auto *state = new detail::callback_state<byte_count_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::sendfile(loop, state->request, out_file, in_file, in_offset, length,
        [state](::uv::raw::fs::request &request, ::uv::raw::fs::byte_count_result result) {
          detail::finish_byte_count(state, request, result);
        });
    });
  }

  inline void sendfile(loop &loop, file_descriptor out_file, file_descriptor in_file, int64_t in_offset,
                       std::size_t length, byte_count_callback callback) {
    sendfile(loop_view{loop.native()}, out_file, in_file, in_offset, length, std::move(callback));
  }

  inline void scandir(loop_view loop, std::string_view path, int flags, scandir_callback callback) {
    auto *state = new detail::callback_state<scandir_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      return ::uv::raw::fs::scandir(loop, state->request, path, flags, [state](::uv::raw::fs::request &request, ::uv::raw::fs::scandir_result result) {
        auto cleanup = request.scoped_cleanup();
        auto callback = std::move(state->callback);
        std::vector<directory_entry> entries;

        if (result) {
          ::uv::raw::fs::directory_entry entry;
          while (result.next(entry)) {
            entries.push_back(directory_entry{std::string{entry.name()}, entry.type()});
          }
        }

        auto out = scandir_result{result.raw(), std::move(entries)};
        cleanup.cleanup();
        delete state;
        callback(std::move(out));
      });
    });
  }

  inline void scandir(loop &loop, std::string_view path, int flags, scandir_callback callback) {
    scandir(loop_view{loop.native()}, path, flags, std::move(callback));
  }

}
