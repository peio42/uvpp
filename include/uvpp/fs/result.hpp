#pragma once

#include <cassert>
#include <cstddef>
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
    class iterator {
    public:
      using value_type = directory_entry;
      using difference_type = std::ptrdiff_t;

      iterator() = default;

      explicit iterator(scandir_result *result) noexcept
        : result_{result} {
        ++(*this);
      }

      const directory_entry &operator*() const noexcept {
        return entry_;
      }

      const directory_entry *operator->() const noexcept {
        return &entry_;
      }

      iterator &operator++() noexcept {
        if (!result_ || !result_->next(entry_)) {
          result_ = nullptr;
        } else {
          ++index_;
        }

        return *this;
      }

      iterator operator++(int) noexcept {
        auto copy = *this;
        ++(*this);
        return copy;
      }

      friend bool operator==(const iterator &lhs, const iterator &rhs) noexcept {
        if (lhs.result_ == nullptr && rhs.result_ == nullptr) {
          return true;
        }

        return lhs.result_ == rhs.result_ && lhs.index_ == rhs.index_;
      }

      friend bool operator!=(const iterator &lhs, const iterator &rhs) noexcept {
        return !(lhs == rhs);
      }

    private:
      scandir_result *result_ = nullptr;
      std::size_t index_ = 0;
      directory_entry entry_;
    };

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

    iterator begin() noexcept {
      return iterator{this};
    }

    iterator end() noexcept {
      return iterator{};
    }

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

    directory_read_buffer::entries_view entries(const directory_read_buffer &buffer) const noexcept {
      return buffer.entries(count());
    }

  private:
    ssize_t result_ = 0;
  };

}
