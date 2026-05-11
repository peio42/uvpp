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
#include "uvpp/fs/file.hpp"
#include "uvpp/fs/result.hpp"
#include "uvpp/net/buffer.hpp"
#include "uvpp/requests/fs.hpp"

namespace uvpp::fs::raw {

  using open_callback = std::function<void(request&, open_result)>;
  using status_callback = std::function<void(request&, status_result)>;
  using byte_count_callback = std::function<void(request&, byte_count_result)>;
  using stat_callback = std::function<void(request&, stat_result)>;
  using path_callback = std::function<void(request&, path_result)>;
  using scandir_callback = std::function<void(request&, scandir_result)>;
  using opendir_callback = std::function<void(request&, opendir_result)>;
  using readdir_callback = std::function<void(request&, readdir_result)>;

  enum class copyfile_flag : int {
    exclusive = UV_FS_COPYFILE_EXCL,
    clone = UV_FS_COPYFILE_FICLONE,
    clone_force = UV_FS_COPYFILE_FICLONE_FORCE
  };

  constexpr int operator|(copyfile_flag lhs, copyfile_flag rhs) noexcept {
    return static_cast<int>(lhs) | static_cast<int>(rhs);
  }

  constexpr int operator|(int lhs, copyfile_flag rhs) noexcept {
    return lhs | static_cast<int>(rhs);
  }

  namespace detail {

    inline void submit(request &req, int status) {
      if (status < 0) {
        req.set_callback({});
        throw error(status);
      }
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

    inline scandir_result make_scandir_result(request &request) noexcept {
      return scandir_result{request.raw_result(), request.native()};
    }

    inline opendir_result make_opendir_result(request &request) noexcept {
      return opendir_result{request.raw_result(), request.ptr()};
    }

    inline readdir_result make_readdir_result(request &request) noexcept {
      return readdir_result{request.raw_result()};
    }

  }

  inline void open(loop_view loop, request &request, std::string_view path, int flags, int mode,
                   open_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_open_result);

    std::string storage{path};
    detail::submit(request, uv_fs_open(loop.native(), request.native(), storage.c_str(), flags, mode,
                                       detail::trampoline));
  }

  inline void open(loop &loop, request &request, std::string_view path, int flags, int mode,
                   open_callback callback) {
    open(loop_view{loop.native()}, request, path, flags, mode, std::move(callback));
  }

  template<auto Callback>
  void open_static(loop_view loop, request &request, std::string_view path, int flags, int mode) {
    std::string storage{path};
    detail::submit(request, uv_fs_open(loop.native(), request.native(), storage.c_str(), flags, mode,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_open_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void open_static(loop &loop, request &request, std::string_view path, int flags, int mode) {
    open_static<Callback>(loop_view{loop.native()}, request, path, flags, mode);
  }

  inline void close(loop_view loop, request &request, file_descriptor file, status_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_status_result);
    detail::submit(request, uv_fs_close(loop.native(), request.native(), file.native(), detail::trampoline));
  }

  inline void close(loop &loop, request &request, file_descriptor file, status_callback callback) {
    close(loop_view{loop.native()}, request, file, std::move(callback));
  }

  template<auto Callback>
  void close_static(loop_view loop, request &request, file_descriptor file) {
    detail::submit(request, uv_fs_close(loop.native(), request.native(), file.native(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_status_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void close_static(loop &loop, request &request, file_descriptor file) {
    close_static<Callback>(loop_view{loop.native()}, request, file);
  }

  inline void read(loop_view loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                   int64_t offset, byte_count_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    detail::submit(request, uv_fs_read(loop.native(), request.native(), file.native(),
                                       reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                       static_cast<unsigned int>(buffers.size()), offset, detail::trampoline));
  }

  inline void read(loop &loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                   int64_t offset, byte_count_callback callback) {
    read(loop_view{loop.native()}, request, file, buffers, offset, std::move(callback));
  }

  inline void read(loop_view loop, request &request, file_descriptor file, const buffer_view &buffer,
                   int64_t offset, byte_count_callback callback) {
    read(loop, request, file, std::span<const buffer_view>{&buffer, 1}, offset, std::move(callback));
  }

  inline void read(loop &loop, request &request, file_descriptor file, const buffer_view &buffer,
                   int64_t offset, byte_count_callback callback) {
    read(loop_view{loop.native()}, request, file, buffer, offset, std::move(callback));
  }

  inline void read(loop_view loop, request &request, file_descriptor file, std::span<std::byte> buffer,
                   int64_t offset, byte_count_callback callback) {
    auto raw = uv_buf_init(reinterpret_cast<char *>(buffer.data()), static_cast<unsigned int>(buffer.size()));
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    detail::submit(request, uv_fs_read(loop.native(), request.native(), file.native(), &raw, 1, offset,
                                       detail::trampoline));
  }

  inline void read(loop &loop, request &request, file_descriptor file, std::span<std::byte> buffer,
                   int64_t offset, byte_count_callback callback) {
    read(loop_view{loop.native()}, request, file, buffer, offset, std::move(callback));
  }

  template<auto Callback>
  void read_static(loop_view loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                   int64_t offset) {
    detail::submit(request, uv_fs_read(loop.native(), request.native(), file.native(),
                                       reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                       static_cast<unsigned int>(buffers.size()), offset,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_byte_count_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void read_static(loop &loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                   int64_t offset) {
    read_static<Callback>(loop_view{loop.native()}, request, file, buffers, offset);
  }

  template<auto Callback>
  void read_static(loop_view loop, request &request, file_descriptor file, const buffer_view &buffer,
                   int64_t offset) {
    read_static<Callback>(loop, request, file, std::span<const buffer_view>{&buffer, 1}, offset);
  }

  template<auto Callback>
  void read_static(loop &loop, request &request, file_descriptor file, const buffer_view &buffer,
                   int64_t offset) {
    read_static<Callback>(loop_view{loop.native()}, request, file, buffer, offset);
  }

  inline void write(loop_view loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                    int64_t offset, byte_count_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    detail::submit(request, uv_fs_write(loop.native(), request.native(), file.native(),
                                        reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                        static_cast<unsigned int>(buffers.size()), offset, detail::trampoline));
  }

  inline void write(loop &loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                    int64_t offset, byte_count_callback callback) {
    write(loop_view{loop.native()}, request, file, buffers, offset, std::move(callback));
  }

  inline void write(loop_view loop, request &request, file_descriptor file, const buffer_view &buffer,
                    int64_t offset, byte_count_callback callback) {
    write(loop, request, file, std::span<const buffer_view>{&buffer, 1}, offset, std::move(callback));
  }

  inline void write(loop &loop, request &request, file_descriptor file, const buffer_view &buffer,
                    int64_t offset, byte_count_callback callback) {
    write(loop_view{loop.native()}, request, file, buffer, offset, std::move(callback));
  }

  inline void write(loop_view loop, request &request, file_descriptor file, std::span<const std::byte> buffer,
                    int64_t offset, byte_count_callback callback) {
    auto raw = uv_buf_init(const_cast<char *>(reinterpret_cast<const char *>(buffer.data())),
                           static_cast<unsigned int>(buffer.size()));
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    detail::submit(request, uv_fs_write(loop.native(), request.native(), file.native(), &raw, 1, offset,
                                        detail::trampoline));
  }

  inline void write(loop &loop, request &request, file_descriptor file, std::span<const std::byte> buffer,
                    int64_t offset, byte_count_callback callback) {
    write(loop_view{loop.native()}, request, file, buffer, offset, std::move(callback));
  }

  template<auto Callback>
  void write_static(loop_view loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                    int64_t offset) {
    detail::submit(request, uv_fs_write(loop.native(), request.native(), file.native(),
                                        reinterpret_cast<const uv_buf_t *>(buffers.data()),
                                        static_cast<unsigned int>(buffers.size()), offset,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_byte_count_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void write_static(loop &loop, request &request, file_descriptor file, std::span<const buffer_view> buffers,
                    int64_t offset) {
    write_static<Callback>(loop_view{loop.native()}, request, file, buffers, offset);
  }

  template<auto Callback>
  void write_static(loop_view loop, request &request, file_descriptor file, const buffer_view &buffer,
                    int64_t offset) {
    write_static<Callback>(loop, request, file, std::span<const buffer_view>{&buffer, 1}, offset);
  }

  template<auto Callback>
  void write_static(loop &loop, request &request, file_descriptor file, const buffer_view &buffer,
                    int64_t offset) {
    write_static<Callback>(loop_view{loop.native()}, request, file, buffer, offset);
  }

  inline void stat(loop_view loop, request &request, std::string_view path, stat_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_stat_result);

    std::string storage{path};
    detail::submit(request, uv_fs_stat(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline void stat(loop &loop, request &request, std::string_view path, stat_callback callback) {
    stat(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  void stat_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    detail::submit(request, uv_fs_stat(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_stat_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void stat_static(loop &loop, request &request, std::string_view path) {
    stat_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline void unlink(loop_view loop, request &request, std::string_view path, status_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_status_result);

    std::string storage{path};
    detail::submit(request, uv_fs_unlink(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline void unlink(loop &loop, request &request, std::string_view path, status_callback callback) {
    unlink(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  void unlink_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    detail::submit(request, uv_fs_unlink(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_status_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void unlink_static(loop &loop, request &request, std::string_view path) {
    unlink_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline void realpath(loop_view loop, request &request, std::string_view path, path_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_path_result);

    std::string storage{path};
    detail::submit(request, uv_fs_realpath(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline void realpath(loop &loop, request &request, std::string_view path, path_callback callback) {
    realpath(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  void realpath_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    detail::submit(request, uv_fs_realpath(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_path_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void realpath_static(loop &loop, request &request, std::string_view path) {
    realpath_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline void readlink(loop_view loop, request &request, std::string_view path, path_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_path_result);

    std::string storage{path};
    detail::submit(request, uv_fs_readlink(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline void readlink(loop &loop, request &request, std::string_view path, path_callback callback) {
    readlink(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  void readlink_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    detail::submit(request, uv_fs_readlink(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_path_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void readlink_static(loop &loop, request &request, std::string_view path) {
    readlink_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline void copyfile(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                       int flags, status_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_status_result);

    std::string path_storage{path};
    std::string new_path_storage{new_path};
    detail::submit(request, uv_fs_copyfile(loop.native(), request.native(), path_storage.c_str(),
                                           new_path_storage.c_str(), flags, detail::trampoline));
  }

  inline void copyfile(loop &loop, request &request, std::string_view path, std::string_view new_path,
                       int flags, status_callback callback) {
    copyfile(loop_view{loop.native()}, request, path, new_path, flags, std::move(callback));
  }

  inline void copyfile(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                       copyfile_flag flag, status_callback callback) {
    copyfile(loop, request, path, new_path, static_cast<int>(flag), std::move(callback));
  }

  inline void copyfile(loop &loop, request &request, std::string_view path, std::string_view new_path,
                       copyfile_flag flag, status_callback callback) {
    copyfile(loop_view{loop.native()}, request, path, new_path, flag, std::move(callback));
  }

  template<auto Callback>
  void copyfile_static(loop_view loop, request &request, std::string_view path, std::string_view new_path,
                       int flags = 0) {
    std::string path_storage{path};
    std::string new_path_storage{new_path};
    detail::submit(request, uv_fs_copyfile(loop.native(), request.native(), path_storage.c_str(),
                                           new_path_storage.c_str(), flags,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_status_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void copyfile_static(loop &loop, request &request, std::string_view path, std::string_view new_path,
                       int flags = 0) {
    copyfile_static<Callback>(loop_view{loop.native()}, request, path, new_path, flags);
  }

  inline void sendfile(loop_view loop, request &request, file_descriptor out_file, file_descriptor in_file,
                       int64_t in_offset, std::size_t length, byte_count_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_byte_count_result);
    detail::submit(request, uv_fs_sendfile(loop.native(), request.native(), out_file.native(), in_file.native(),
                                           in_offset, length, detail::trampoline));
  }

  inline void sendfile(loop &loop, request &request, file_descriptor out_file, file_descriptor in_file,
                       int64_t in_offset, std::size_t length, byte_count_callback callback) {
    sendfile(loop_view{loop.native()}, request, out_file, in_file, in_offset, length, std::move(callback));
  }

  template<auto Callback>
  void sendfile_static(loop_view loop, request &request, file_descriptor out_file, file_descriptor in_file,
                       int64_t in_offset, std::size_t length) {
    detail::submit(request, uv_fs_sendfile(loop.native(), request.native(), out_file.native(), in_file.native(),
                                           in_offset, length,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_byte_count_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void sendfile_static(loop &loop, request &request, file_descriptor out_file, file_descriptor in_file,
                       int64_t in_offset, std::size_t length) {
    sendfile_static<Callback>(loop_view{loop.native()}, request, out_file, in_file, in_offset, length);
  }

  inline void scandir(loop_view loop, request &request, std::string_view path, int flags,
                      scandir_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_scandir_result);

    std::string storage{path};
    detail::submit(request, uv_fs_scandir(loop.native(), request.native(), storage.c_str(), flags,
                                          detail::trampoline));
  }

  inline void scandir(loop &loop, request &request, std::string_view path, int flags,
                      scandir_callback callback) {
    scandir(loop_view{loop.native()}, request, path, flags, std::move(callback));
  }

  template<auto Callback>
  void scandir_static(loop_view loop, request &request, std::string_view path, int flags = 0) {
    std::string storage{path};
    detail::submit(request, uv_fs_scandir(loop.native(), request.native(), storage.c_str(), flags,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_scandir_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void scandir_static(loop &loop, request &request, std::string_view path, int flags = 0) {
    scandir_static<Callback>(loop_view{loop.native()}, request, path, flags);
  }

  inline void opendir(loop_view loop, request &request, std::string_view path, opendir_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_opendir_result);

    std::string storage{path};
    detail::submit(request, uv_fs_opendir(loop.native(), request.native(), storage.c_str(), detail::trampoline));
  }

  inline void opendir(loop &loop, request &request, std::string_view path, opendir_callback callback) {
    opendir(loop_view{loop.native()}, request, path, std::move(callback));
  }

  template<auto Callback>
  void opendir_static(loop_view loop, request &request, std::string_view path) {
    std::string storage{path};
    detail::submit(request, uv_fs_opendir(loop.native(), request.native(), storage.c_str(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_opendir_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, std::move(result));
      }));
  }

  template<auto Callback>
  void opendir_static(loop &loop, request &request, std::string_view path) {
    opendir_static<Callback>(loop_view{loop.native()}, request, path);
  }

  inline void readdir(loop_view loop, request &request, directory &dir, directory_read_buffer &buffer,
                      readdir_callback callback) {
    buffer.prepare(dir);
    detail::set_callback(request, std::move(callback), detail::make_readdir_result);
    detail::submit(request, uv_fs_readdir(loop.native(), request.native(), dir.native(), detail::trampoline));
  }

  inline void readdir(loop &loop, request &request, directory &dir, directory_read_buffer &buffer,
                      readdir_callback callback) {
    readdir(loop_view{loop.native()}, request, dir, buffer, std::move(callback));
  }

  template<auto Callback>
  void readdir_static(loop_view loop, request &request, directory &dir, directory_read_buffer &buffer) {
    buffer.prepare(dir);
    detail::submit(request, uv_fs_readdir(loop.native(), request.native(), dir.native(),
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_readdir_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
  }

  template<auto Callback>
  void readdir_static(loop &loop, request &request, directory &dir, directory_read_buffer &buffer) {
    readdir_static<Callback>(loop_view{loop.native()}, request, dir, buffer);
  }

  inline void closedir(loop_view loop, request &request, directory &&dir, status_callback callback) {
    detail::set_callback(request, std::move(callback), detail::make_status_result);
    auto *native_dir = dir.native();
    detail::submit(request, uv_fs_closedir(loop.native(), request.native(), native_dir, detail::trampoline));
    dir.release();
  }

  inline void closedir(loop &loop, request &request, directory &&dir, status_callback callback) {
    closedir(loop_view{loop.native()}, request, std::move(dir), std::move(callback));
  }

  template<auto Callback>
  void closedir_static(loop_view loop, request &request, directory &&dir) {
    auto *native_dir = dir.native();
    detail::submit(request, uv_fs_closedir(loop.native(), request.native(), native_dir,
      [](uv_fs_t *raw) noexcept {
        auto &req = request::from_native(raw);
        auto result = detail::make_status_result(req);
        uvpp::detail::invoke_static_callback<Callback>(req, result);
      }));
    dir.release();
  }

  template<auto Callback>
  void closedir_static(loop &loop, request &request, directory &&dir) {
    closedir_static<Callback>(loop_view{loop.native()}, request, std::move(dir));
  }

}

namespace uvpp::fs {

  using raw::copyfile_flag;
  using raw::directory_entry_type;

  class status_result {
  public:
    status_result() = default;
    explicit status_result(ssize_t result) noexcept : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    int status() const noexcept { return static_cast<int>(result_); }
    std::error_code error_code() const noexcept { return make_error_code(status()); }

  private:
    ssize_t result_ = 0;
  };

  class open_result {
  public:
    explicit open_result(ssize_t result) noexcept : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    std::error_code error_code() const noexcept { return make_error_code(static_cast<int>(result_)); }

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
    std::error_code error_code() const noexcept { return make_error_code(static_cast<int>(result_)); }

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
    std::error_code error_code() const noexcept { return make_error_code(static_cast<int>(result_)); }

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
      if (stat) {
        stat_ = *stat;
      }
    }

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    std::error_code error_code() const noexcept { return make_error_code(static_cast<int>(result_)); }
    const uv_stat_t &native() const noexcept { return stat_; }

  private:
    ssize_t result_ = 0;
    uv_stat_t stat_{};
  };

  class path_result {
  public:
    path_result(ssize_t result, std::string path) : result_{result}, path_{std::move(path)} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    std::error_code error_code() const noexcept { return make_error_code(static_cast<int>(result_)); }
    std::string_view path() const noexcept { return path_; }
    std::string take_path() { return std::move(path_); }

  private:
    ssize_t result_ = 0;
    std::string path_;
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
    std::error_code error_code() const noexcept { return make_error_code(static_cast<int>(result_)); }

    std::span<const directory_entry> entries() const noexcept {
      return entries_;
    }

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
  using read_callback = std::function<void(read_result)>;
  using stat_callback = std::function<void(stat_result)>;
  using path_callback = std::function<void(path_result)>;
  using scandir_callback = std::function<void(scandir_result)>;

  namespace detail {

    template<class Callback>
    struct callback_state {
      raw::request request;
      Callback callback;
    };

    template<class State, class Submit>
    void submit_owned(State *state, Submit submit) {
      try {
        submit();
      } catch (...) {
        delete state;
        throw;
      }
    }

    inline void finish_status(callback_state<status_callback> *state, raw::request &request,
                              raw::status_result result) {
      auto cleanup = request.scoped_cleanup();
      auto callback = std::move(state->callback);
      auto out = status_result{result.raw()};
      cleanup.cleanup();
      delete state;
      callback(out);
    }

    inline void finish_byte_count(callback_state<byte_count_callback> *state, raw::request &request,
                                  raw::byte_count_result result) {
      auto cleanup = request.scoped_cleanup();
      auto callback = std::move(state->callback);
      auto out = byte_count_result{result.raw()};
      cleanup.cleanup();
      delete state;
      callback(out);
    }

  }

  inline void open(loop_view loop, std::string_view path, int flags, int mode, open_callback callback) {
    auto *state = new detail::callback_state<open_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      raw::open(loop, state->request, path, flags, mode, [state](raw::request &request, raw::open_result result) {
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
      raw::close(loop, state->request, file, [state](raw::request &request, raw::status_result result) {
        detail::finish_status(state, request, result);
      });
    });
  }

  inline void close(loop &loop, file_descriptor file, status_callback callback) {
    close(loop_view{loop.native()}, file, std::move(callback));
  }

  inline void read(loop_view loop, file_descriptor file, std::size_t size, int64_t offset, read_callback callback) {
    struct state_type {
      raw::request request;
      read_callback callback;
      owned_buffer buffer;
    };

    auto *state = new state_type{{}, std::move(callback), owned_buffer{size}};
    detail::submit_owned(state, [&] {
      raw::read(loop, state->request, file, state->buffer.bytes(), offset,
        [state](raw::request &request, raw::byte_count_result result) {
          auto cleanup = request.scoped_cleanup();
          auto callback = std::move(state->callback);
          auto out = read_result{result.raw(), std::move(state->buffer)};
          cleanup.cleanup();
          delete state;
          callback(std::move(out));
        });
    });
  }

  inline void read(loop &loop, file_descriptor file, std::size_t size, int64_t offset, read_callback callback) {
    read(loop_view{loop.native()}, file, size, offset, std::move(callback));
  }

  inline void write(loop_view loop, file_descriptor file, std::span<const std::byte> bytes, int64_t offset,
                    byte_count_callback callback) {
    struct state_type {
      raw::request request;
      byte_count_callback callback;
      owned_buffer buffer;
    };

    auto *state = new state_type{{}, std::move(callback), owned_buffer{bytes}};
    detail::submit_owned(state, [&] {
      raw::write(loop, state->request, file, state->buffer.bytes(), offset,
        [state](raw::request &request, raw::byte_count_result result) {
          auto cleanup = request.scoped_cleanup();
          auto callback = std::move(state->callback);
          auto out = byte_count_result{result.raw()};
          cleanup.cleanup();
          delete state;
          callback(out);
        });
    });
  }

  inline void write(loop &loop, file_descriptor file, std::span<const std::byte> bytes, int64_t offset,
                    byte_count_callback callback) {
    write(loop_view{loop.native()}, file, bytes, offset, std::move(callback));
  }

  inline void stat(loop_view loop, std::string_view path, stat_callback callback) {
    auto *state = new detail::callback_state<stat_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      raw::stat(loop, state->request, path, [state](raw::request &request, raw::stat_result result) {
        auto cleanup = request.scoped_cleanup();
        auto callback = std::move(state->callback);
        auto out = stat_result{result.raw(), result.native_ptr()};
        cleanup.cleanup();
        delete state;
        callback(out);
      });
    });
  }

  inline void stat(loop &loop, std::string_view path, stat_callback callback) {
    stat(loop_view{loop.native()}, path, std::move(callback));
  }

  inline void unlink(loop_view loop, std::string_view path, status_callback callback) {
    auto *state = new detail::callback_state<status_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      raw::unlink(loop, state->request, path, [state](raw::request &request, raw::status_result result) {
        detail::finish_status(state, request, result);
      });
    });
  }

  inline void unlink(loop &loop, std::string_view path, status_callback callback) {
    unlink(loop_view{loop.native()}, path, std::move(callback));
  }

  inline void realpath(loop_view loop, std::string_view path, path_callback callback) {
    auto *state = new detail::callback_state<path_callback>{{}, std::move(callback)};
    detail::submit_owned(state, [&] {
      raw::realpath(loop, state->request, path, [state](raw::request &request, raw::path_result result) {
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
      raw::readlink(loop, state->request, path, [state](raw::request &request, raw::path_result result) {
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
      raw::copyfile(loop, state->request, path, new_path, flags,
        [state](raw::request &request, raw::status_result result) {
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
      raw::sendfile(loop, state->request, out_file, in_file, in_offset, length,
        [state](raw::request &request, raw::byte_count_result result) {
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
      raw::scandir(loop, state->request, path, flags, [state](raw::request &request, raw::scandir_result result) {
        auto cleanup = request.scoped_cleanup();
        auto callback = std::move(state->callback);
        std::vector<directory_entry> entries;

        if (result) {
          raw::directory_entry entry;
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
