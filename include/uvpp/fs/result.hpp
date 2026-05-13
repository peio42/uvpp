#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

#include <uv.h>

#include "uvpp/core/error.hpp"
#include "uvpp/fs/dir.hpp"
#include "uvpp/fs/file.hpp"

namespace uv::fs::raw {

  class status_result {
  public:
    status_result() = default;

    explicit status_result(ssize_t result) noexcept
      : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }
    int status() const noexcept { return static_cast<int>(result_); }

    std::error_code error_code() const noexcept {
      return make_error_code(status());
    }

  private:
    ssize_t result_ = 0;
  };

  class open_result {
  public:
    explicit open_result(ssize_t result) noexcept
      : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }

    std::error_code error_code() const noexcept {
      return make_error_code(static_cast<int>(result_));
    }

    file_descriptor file() const noexcept {
      return ok() ? file_descriptor{static_cast<uv_file>(result_)} : file_descriptor{};
    }

  private:
    ssize_t result_ = 0;
  };

  class byte_count_result {
  public:
    explicit byte_count_result(ssize_t result) noexcept
      : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }

    std::error_code error_code() const noexcept {
      return make_error_code(static_cast<int>(result_));
    }

    std::size_t count() const noexcept {
      return ok() ? static_cast<std::size_t>(result_) : 0;
    }

  private:
    ssize_t result_ = 0;
  };

  class stat_result {
  public:
    stat_result(ssize_t result, const uv_stat_t *statbuf) noexcept
      : result_{result}, stat_{statbuf} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    int status() const noexcept { return static_cast<int>(result_); }
    ssize_t raw() const noexcept { return result_; }

    std::error_code error_code() const noexcept {
      return make_error_code(status());
    }

    const uv_stat_t &native() const noexcept {
      assert(stat_ != nullptr);
      return *stat_;
    }

    const uv_stat_t *native_ptr() const noexcept {
      return stat_;
    }

  private:
    ssize_t result_ = 0;
    const uv_stat_t *stat_ = nullptr;
  };

  class path_result {
  public:
    path_result(ssize_t result, const char *path) noexcept
      : result_{result}, path_{path} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }

    std::error_code error_code() const noexcept {
      return make_error_code(static_cast<int>(result_));
    }

    std::string_view path() const noexcept {
      return ok() && path_ ? std::string_view{path_} : std::string_view{};
    }

  private:
    ssize_t result_ = 0;
    const char *path_ = nullptr;
  };

  class scandir_result {
  public:
    scandir_result(ssize_t result, uv_fs_t *request) noexcept
      : result_{result}, request_{request} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }

    std::error_code error_code() const noexcept {
      return make_error_code(static_cast<int>(result_));
    }

    std::size_t count() const noexcept {
      return ok() ? static_cast<std::size_t>(result_) : 0;
    }

    bool next(directory_entry &entry) noexcept {
      if (!ok()) {
        return false;
      }

      uv_dirent_t native{};
      if (uv_fs_scandir_next(request_, &native) != 0) {
        return false;
      }

      entry = directory_entry{native.name, native.type};
      return true;
    }

    // Input iterator over the entries returned by uv_fs_scandir_next.
    // scandir_result is a single-pass range: the internal cursor advances with
    // each dereference, so begin() must be called at most once.
    class iterator {
    public:
      using value_type        = directory_entry;
      using difference_type   = std::ptrdiff_t;
      using iterator_category = std::input_iterator_tag;
      using iterator_concept  = std::input_iterator_tag;

      iterator() = default;

      const directory_entry &operator*()  const noexcept { return current_; }
      const directory_entry *operator->() const noexcept { return &current_; }

      iterator &operator++() { advance(); return *this; }
      void      operator++(int) { advance(); }

      bool operator==(const iterator &other) const noexcept {
        return req_ == other.req_;
      }

    private:
      friend class scandir_result;

      explicit iterator(uv_fs_t *req) : req_{req} { advance(); }

      void advance() noexcept {
        uv_dirent_t native{};
        if (!req_ || uv_fs_scandir_next(req_, &native) != 0) {
          req_ = nullptr;
        } else {
          current_ = directory_entry{native.name, native.type};
        }
      }

      uv_fs_t *req_ = nullptr;
      directory_entry current_;
    };

    iterator begin() noexcept {
      return ok() ? iterator{request_} : iterator{};
    }

    iterator end() const noexcept { return {}; }

  private:
    ssize_t result_ = 0;
    uv_fs_t *request_ = nullptr;
  };

  class opendir_result {
  public:
    opendir_result(ssize_t result, void *ptr) noexcept
      : result_{result}, dir_{static_cast<uv_dir_t *>(ptr)} {}

    opendir_result(const opendir_result&) = delete;
    opendir_result& operator=(const opendir_result&) = delete;

    opendir_result(opendir_result &&other) noexcept
      : result_{other.result_}, dir_{std::exchange(other.dir_, nullptr)} {}

    opendir_result& operator=(opendir_result &&other) noexcept {
      if (this != &other) {
        assert(dir_ == nullptr);
        result_ = other.result_;
        dir_ = std::exchange(other.dir_, nullptr);
      }

      return *this;
    }

    ~opendir_result() {
      assert(dir_ == nullptr);
    }

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    ssize_t raw() const noexcept { return result_; }

    std::error_code error_code() const noexcept {
      return make_error_code(static_cast<int>(result_));
    }

    directory take_directory() noexcept {
      return ok() ? directory{std::exchange(dir_, nullptr)} : directory{};
    }

  private:
    ssize_t result_ = 0;
    uv_dir_t *dir_ = nullptr;
  };

  class readdir_result {
  public:
    explicit readdir_result(ssize_t result) noexcept
      : result_{result} {}

    bool ok() const noexcept { return result_ >= 0; }
    explicit operator bool() const noexcept { return ok(); }
    bool eof() const noexcept { return result_ == 0; }
    ssize_t raw() const noexcept { return result_; }

    std::error_code error_code() const noexcept {
      return make_error_code(static_cast<int>(result_));
    }

    std::size_t count() const noexcept {
      return ok() ? static_cast<std::size_t>(result_) : 0;
    }

  private:
    ssize_t result_ = 0;
  };

}
