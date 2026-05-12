#pragma once

#include <cassert>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include <uv.h>

namespace uvpp::fs::raw {

  enum class directory_entry_type {
    unknown = UV_DIRENT_UNKNOWN,
    file = UV_DIRENT_FILE,
    directory = UV_DIRENT_DIR,
    link = UV_DIRENT_LINK,
    fifo = UV_DIRENT_FIFO,
    socket = UV_DIRENT_SOCKET,
    character = UV_DIRENT_CHAR,
    block = UV_DIRENT_BLOCK
  };

  class directory_entry {
  public:
    directory_entry() = default;

    directory_entry(const char *name, uv_dirent_type_t type) noexcept
      : name_{name ? std::string_view{name} : std::string_view{}},
        type_{static_cast<directory_entry_type>(type)} {}

    std::string_view name() const noexcept {
      return name_;
    }

    directory_entry_type type() const noexcept {
      return type_;
    }

  private:
    std::string_view name_;
    directory_entry_type type_ = directory_entry_type::unknown;
  };

  class directory {
  public:
    directory() = default;

    explicit directory(uv_dir_t *dir) noexcept
      : dir_{dir} {}

    directory(const directory&) = delete;
    directory& operator=(const directory&) = delete;

    directory(directory &&other) noexcept
      : dir_{other.release()} {}

    directory& operator=(directory &&other) noexcept {
      if (this != &other) {
        assert(dir_ == nullptr);
        dir_ = other.release();
      }

      return *this;
    }

    ~directory() {
      assert(dir_ == nullptr);
    }

    uv_dir_t *native() noexcept {
      return dir_;
    }

    const uv_dir_t *native() const noexcept {
      return dir_;
    }

    explicit operator bool() const noexcept {
      return dir_ != nullptr;
    }

    uv_dir_t *release() noexcept {
      auto *out = dir_;
      dir_ = nullptr;
      return out;
    }

  private:
    uv_dir_t *dir_ = nullptr;
  };

  class directory_read_buffer {
  public:
    explicit directory_read_buffer(std::size_t capacity)
      : entries_(capacity) {}

    std::size_t capacity() const noexcept {
      return entries_.size();
    }

    void prepare(directory &dir) noexcept {
      dir.native()->dirents = entries_.data();
      dir.native()->nentries = entries_.size();
    }

    std::span<const uv_dirent_t> native_entries(std::size_t count) const noexcept {
      return {entries_.data(), count <= entries_.size() ? count : entries_.size()};
    }

    directory_entry entry(std::size_t index) const noexcept {
      const auto &entry = entries_[index];
      return directory_entry{entry.name, entry.type};
    }

  private:
    std::vector<uv_dirent_t> entries_;
  };

}
