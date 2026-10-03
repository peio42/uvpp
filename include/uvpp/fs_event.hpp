#pragma once

#include <optional>
#include <string>

#include <uv.h>

namespace uv {

enum class fs_event_flag : unsigned {
  rename = UV_RENAME,
  change = UV_CHANGE,
};

enum class fs_event_option : unsigned {
  watch_entry = UV_FS_EVENT_WATCH_ENTRY,
  stat = UV_FS_EVENT_STAT,
  recursive = UV_FS_EVENT_RECURSIVE,
};

class fs_event_flags {
public:
  constexpr fs_event_flags() noexcept = default;
  constexpr fs_event_flags(fs_event_flag flag) noexcept : flags_{static_cast<unsigned>(flag)} {}
  static constexpr fs_event_flags from_raw(unsigned flags) noexcept {
    fs_event_flags result;
    result.flags_ = flags;
    return result;
  }
  constexpr unsigned raw() const noexcept { return flags_; }
  constexpr explicit operator bool() const noexcept { return flags_ != 0; }
  constexpr bool has(fs_event_flag flag) const noexcept {
    return (flags_ & static_cast<unsigned>(flag)) != 0;
  }
  constexpr fs_event_flags &operator|=(fs_event_flags flags) noexcept {
    flags_ |= flags.flags_;
    return *this;
  }

private:
  unsigned flags_ = 0;
};

constexpr fs_event_flags operator|(fs_event_flags lhs, fs_event_flags rhs) noexcept {
  return lhs |= rhs;
}
constexpr fs_event_flags operator|(fs_event_flag lhs, fs_event_flag rhs) noexcept {
  return fs_event_flags{lhs} | fs_event_flags{rhs};
}

class fs_event_options {
public:
  constexpr fs_event_options() noexcept = default;
  constexpr fs_event_options(fs_event_option option) noexcept : options_{static_cast<unsigned>(option)} {}
  static constexpr fs_event_options from_raw(unsigned options) noexcept {
    fs_event_options result;
    result.options_ = options;
    return result;
  }
  constexpr unsigned raw() const noexcept { return options_; }
  constexpr explicit operator bool() const noexcept { return options_ != 0; }
  constexpr bool has(fs_event_option option) const noexcept {
    return (options_ & static_cast<unsigned>(option)) != 0;
  }
  constexpr fs_event_options &operator|=(fs_event_options options) noexcept {
    options_ |= options.options_;
    return *this;
  }

private:
  unsigned options_ = 0;
};

constexpr fs_event_options operator|(fs_event_options lhs, fs_event_options rhs) noexcept {
  return lhs |= rhs;
}
constexpr fs_event_options operator|(fs_event_option lhs, fs_event_option rhs) noexcept {
  return fs_event_options{lhs} | fs_event_options{rhs};
}

struct fs_event {
  std::optional<std::string> filename;
  fs_event_flags events;
};

} // namespace uv
