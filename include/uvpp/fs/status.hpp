#pragma once

#include <chrono>
#include <cstdint>
#include <sys/stat.h>

#include <uv.h>

namespace uv::fs {

  enum class file_type {
    none,
    regular,
    directory,
    symlink,
    block,
    character,
    fifo,
    socket,
    unknown
  };

  enum class file_permission : unsigned int {
    none = 0,
    owner_read = 0400,
    owner_write = 0200,
    owner_execute = 0100,
    group_read = 0040,
    group_write = 0020,
    group_execute = 0010,
    others_read = 0004,
    others_write = 0002,
    others_execute = 0001,
    set_uid = 04000,
    set_gid = 02000,
    sticky = 01000
  };

  constexpr file_permission operator|(file_permission lhs, file_permission rhs) noexcept {
    return static_cast<file_permission>(static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
  }

  constexpr file_permission operator&(file_permission lhs, file_permission rhs) noexcept {
    return static_cast<file_permission>(static_cast<unsigned int>(lhs) & static_cast<unsigned int>(rhs));
  }

  constexpr file_permission& operator|=(file_permission &lhs, file_permission rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
  }

  using file_time = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

  namespace status_detail {

    inline file_type file_type_from_mode(unsigned int mode) noexcept {
#if defined(S_ISREG)
      if (S_ISREG(mode)) return file_type::regular;
#elif defined(S_IFMT) && defined(S_IFREG)
      if ((mode & S_IFMT) == S_IFREG) return file_type::regular;
#endif
#if defined(S_ISDIR)
      if (S_ISDIR(mode)) return file_type::directory;
#elif defined(S_IFMT) && defined(S_IFDIR)
      if ((mode & S_IFMT) == S_IFDIR) return file_type::directory;
#endif
#if defined(S_ISLNK)
      if (S_ISLNK(mode)) return file_type::symlink;
#elif defined(S_IFMT) && defined(S_IFLNK)
      if ((mode & S_IFMT) == S_IFLNK) return file_type::symlink;
#endif
#if defined(S_ISBLK)
      if (S_ISBLK(mode)) return file_type::block;
#elif defined(S_IFMT) && defined(S_IFBLK)
      if ((mode & S_IFMT) == S_IFBLK) return file_type::block;
#endif
#if defined(S_ISCHR)
      if (S_ISCHR(mode)) return file_type::character;
#elif defined(S_IFMT) && defined(S_IFCHR)
      if ((mode & S_IFMT) == S_IFCHR) return file_type::character;
#endif
#if defined(S_ISFIFO)
      if (S_ISFIFO(mode)) return file_type::fifo;
#elif defined(S_IFMT) && defined(S_IFIFO)
      if ((mode & S_IFMT) == S_IFIFO) return file_type::fifo;
#endif
#if defined(S_ISSOCK)
      if (S_ISSOCK(mode)) return file_type::socket;
#elif defined(S_IFMT) && defined(S_IFSOCK)
      if ((mode & S_IFMT) == S_IFSOCK) return file_type::socket;
#endif

      return mode == 0 ? file_type::none : file_type::unknown;
    }

    inline file_time file_time_from_timespec(uv_timespec_t time) noexcept {
      return file_time{
        std::chrono::seconds{time.tv_sec} + std::chrono::nanoseconds{time.tv_nsec}
      };
    }

  }

  class file_status {
  public:
    file_status() = default;

    explicit file_status(const uv_stat_t &stat) noexcept
      : type_{status_detail::file_type_from_mode(static_cast<unsigned int>(stat.st_mode))},
        size_{stat.st_size >= 0 ? static_cast<std::uint64_t>(stat.st_size) : 0},
        permissions_{static_cast<file_permission>(static_cast<unsigned int>(stat.st_mode) & 07777u)},
        access_time_{status_detail::file_time_from_timespec(stat.st_atim)},
        modification_time_{status_detail::file_time_from_timespec(stat.st_mtim)},
        change_time_{status_detail::file_time_from_timespec(stat.st_ctim)},
        birth_time_{status_detail::file_time_from_timespec(stat.st_birthtim)} {}

    file_type type() const noexcept { return type_; }

    bool is_regular() const noexcept { return type_ == file_type::regular; }
    bool is_directory() const noexcept { return type_ == file_type::directory; }
    bool is_symlink() const noexcept { return type_ == file_type::symlink; }

    std::uint64_t size() const noexcept { return size_; }

    file_permission permissions() const noexcept { return permissions_; }
    unsigned int raw_permissions() const noexcept {
      return static_cast<unsigned int>(permissions_);
    }

    bool has_permission(file_permission permission) const noexcept {
      return (permissions_ & permission) == permission;
    }

    file_time access_time() const noexcept { return access_time_; }
    file_time modification_time() const noexcept { return modification_time_; }
    file_time change_time() const noexcept { return change_time_; }
    file_time birth_time() const noexcept { return birth_time_; }

  private:
    file_type type_ = file_type::none;
    std::uint64_t size_ = 0;
    file_permission permissions_ = file_permission::none;
    file_time access_time_{};
    file_time modification_time_{};
    file_time change_time_{};
    file_time birth_time_{};
  };

}
