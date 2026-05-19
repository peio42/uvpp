#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"
#include "uvpp/handles/stream.hpp"

namespace uv {

  struct process_exit {
    int64_t status = 0;
    int signal = 0;
  };

  struct process_stdio {
    static uv_stdio_container_t ignore() noexcept {
      auto out = uv_stdio_container_t{};
      out.flags = UV_IGNORE;
      return out;
    }

    static uv_stdio_container_t inherit_fd(int fd) noexcept {
      auto out = uv_stdio_container_t{};
      out.flags = UV_INHERIT_FD;
      out.data.fd = fd;
      return out;
    }

    template<stream_handle Stream>
    static uv_stdio_container_t inherit_stream(Stream &stream) noexcept {
      auto out = uv_stdio_container_t{};
      out.flags = UV_INHERIT_STREAM;
      out.data.stream = stream.native_stream();
      return out;
    }

    template<stream_handle Stream>
    static uv_stdio_container_t create_pipe(Stream &stream, unsigned int flags) noexcept {
      auto out = uv_stdio_container_t{};
      out.flags = static_cast<uv_stdio_flags>(UV_CREATE_PIPE | flags);
      out.data.stream = stream.native_stream();
      return out;
    }

    template<stream_handle Stream>
    static uv_stdio_container_t readable_pipe(Stream &stream) noexcept {
      return create_pipe(stream, UV_READABLE_PIPE);
    }

    template<stream_handle Stream>
    static uv_stdio_container_t writable_pipe(Stream &stream) noexcept {
      return create_pipe(stream, UV_WRITABLE_PIPE);
    }

    template<stream_handle Stream>
    static uv_stdio_container_t duplex_pipe(Stream &stream) noexcept {
      return create_pipe(stream, UV_READABLE_PIPE | UV_WRITABLE_PIPE);
    }
  };

  struct process_options {
    std::string file;
    std::vector<std::string> arguments;
    bool inherit_parent_environment = true;
    std::vector<std::string> environment;
    std::string working_directory;
    unsigned int flags = 0;
    uv_uid_t uid = 0;
    uv_gid_t gid = 0;
    std::vector<uv_stdio_container_t> stdio_entries;

    static process_options make(std::string_view file) {
      auto out = process_options{};
      out.file = std::string{file};
      return out;
    }

    process_options &arg(std::string_view value) & {
      add_argument(value);
      return *this;
    }

    process_options &&arg(std::string_view value) && {
      add_argument(value);
      return std::move(*this);
    }

    process_options &args(std::initializer_list<std::string_view> values) & {
      add_arguments(values);
      return *this;
    }

    process_options &&args(std::initializer_list<std::string_view> values) && {
      add_arguments(values);
      return std::move(*this);
    }

    process_options &cwd(std::string_view path) & {
      working_directory = std::string{path};
      return *this;
    }

    process_options &&cwd(std::string_view path) && {
      working_directory = std::string{path};
      return std::move(*this);
    }

    process_options &inherit_environment() & noexcept {
      inherit_parent_environment = true;
      environment.clear();
      return *this;
    }

    process_options &&inherit_environment() && noexcept {
      inherit_parent_environment = true;
      environment.clear();
      return std::move(*this);
    }

    process_options &empty_environment() & noexcept {
      inherit_parent_environment = false;
      environment.clear();
      return *this;
    }

    process_options &&empty_environment() && noexcept {
      inherit_parent_environment = false;
      environment.clear();
      return std::move(*this);
    }

    process_options &env(std::string_view entry) & {
      add_environment(entry);
      return *this;
    }

    process_options &&env(std::string_view entry) && {
      add_environment(entry);
      return std::move(*this);
    }

    process_options &env(std::string_view key, std::string_view value) & {
      add_environment(key, value);
      return *this;
    }

    process_options &&env(std::string_view key, std::string_view value) && {
      add_environment(key, value);
      return std::move(*this);
    }

    process_options &stdio(std::initializer_list<uv_stdio_container_t> entries) & {
      stdio_entries.assign(entries.begin(), entries.end());
      return *this;
    }

    process_options &&stdio(std::initializer_list<uv_stdio_container_t> entries) && {
      stdio_entries.assign(entries.begin(), entries.end());
      return std::move(*this);
    }

    process_options &stdio(std::span<const uv_stdio_container_t> entries) & {
      stdio_entries.assign(entries.begin(), entries.end());
      return *this;
    }

    process_options &&stdio(std::span<const uv_stdio_container_t> entries) && {
      stdio_entries.assign(entries.begin(), entries.end());
      return std::move(*this);
    }

    process_options &ignore_stdin() & { return set_stdio(0, process_stdio::ignore()); }
    process_options &&ignore_stdin() && { set_stdio(0, process_stdio::ignore()); return std::move(*this); }

    process_options &ignore_stdout() & { return set_stdio(1, process_stdio::ignore()); }
    process_options &&ignore_stdout() && { set_stdio(1, process_stdio::ignore()); return std::move(*this); }

    process_options &ignore_stderr() & { return set_stdio(2, process_stdio::ignore()); }
    process_options &&ignore_stderr() && { set_stdio(2, process_stdio::ignore()); return std::move(*this); }

    process_options &inherit_stdin() & { return set_stdio(0, process_stdio::inherit_fd(0)); }
    process_options &&inherit_stdin() && { set_stdio(0, process_stdio::inherit_fd(0)); return std::move(*this); }

    process_options &inherit_stdout() & { return set_stdio(1, process_stdio::inherit_fd(1)); }
    process_options &&inherit_stdout() && { set_stdio(1, process_stdio::inherit_fd(1)); return std::move(*this); }

    process_options &inherit_stderr() & { return set_stdio(2, process_stdio::inherit_fd(2)); }
    process_options &&inherit_stderr() && { set_stdio(2, process_stdio::inherit_fd(2)); return std::move(*this); }

    process_options &inherit_stdio() & {
      inherit_stdin();
      inherit_stdout();
      inherit_stderr();
      return *this;
    }

    process_options &&inherit_stdio() && {
      inherit_stdin();
      inherit_stdout();
      inherit_stderr();
      return std::move(*this);
    }

    template<stream_handle Stream>
    process_options &pipe_stdin(Stream &stream) & {
      return set_stdio(0, process_stdio::readable_pipe(stream));
    }

    template<stream_handle Stream>
    process_options &&pipe_stdin(Stream &stream) && {
      set_stdio(0, process_stdio::readable_pipe(stream));
      return std::move(*this);
    }

    template<stream_handle Stream>
    process_options &pipe_stdout(Stream &stream) & {
      return set_stdio(1, process_stdio::writable_pipe(stream));
    }

    template<stream_handle Stream>
    process_options &&pipe_stdout(Stream &stream) && {
      set_stdio(1, process_stdio::writable_pipe(stream));
      return std::move(*this);
    }

    template<stream_handle Stream>
    process_options &pipe_stderr(Stream &stream) & {
      return set_stdio(2, process_stdio::writable_pipe(stream));
    }

    template<stream_handle Stream>
    process_options &&pipe_stderr(Stream &stream) && {
      set_stdio(2, process_stdio::writable_pipe(stream));
      return std::move(*this);
    }

    process_options &detached(bool enable = true) & noexcept {
      set_process_flag(UV_PROCESS_DETACHED, enable);
      return *this;
    }

    process_options &&detached(bool enable = true) && noexcept {
      set_process_flag(UV_PROCESS_DETACHED, enable);
      return std::move(*this);
    }

    process_options &set_uid(uv_uid_t value) & noexcept {
      uid = value;
      set_process_flag(UV_PROCESS_SETUID, true);
      return *this;
    }

    process_options &&set_uid(uv_uid_t value) && noexcept {
      uid = value;
      set_process_flag(UV_PROCESS_SETUID, true);
      return std::move(*this);
    }

    process_options &set_gid(uv_gid_t value) & noexcept {
      gid = value;
      set_process_flag(UV_PROCESS_SETGID, true);
      return *this;
    }

    process_options &&set_gid(uv_gid_t value) && noexcept {
      gid = value;
      set_process_flag(UV_PROCESS_SETGID, true);
      return std::move(*this);
    }

    process_options &windows_verbatim_arguments(bool enable = true) & noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS, enable);
      return *this;
    }

    process_options &&windows_verbatim_arguments(bool enable = true) && noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS, enable);
      return std::move(*this);
    }

    process_options &windows_hide(bool enable = true) & noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_HIDE, enable);
      return *this;
    }

    process_options &&windows_hide(bool enable = true) && noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_HIDE, enable);
      return std::move(*this);
    }

    process_options &windows_hide_console(bool enable = true) & noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_HIDE_CONSOLE, enable);
      return *this;
    }

    process_options &&windows_hide_console(bool enable = true) && noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_HIDE_CONSOLE, enable);
      return std::move(*this);
    }

    process_options &windows_hide_gui(bool enable = true) & noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_HIDE_GUI, enable);
      return *this;
    }

    process_options &&windows_hide_gui(bool enable = true) && noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_HIDE_GUI, enable);
      return std::move(*this);
    }

    process_options &set_stdio(std::size_t fd, uv_stdio_container_t entry) & {
      if (stdio_entries.size() <= fd) {
        stdio_entries.resize(fd + 1);
      }
      stdio_entries[fd] = entry;
      return *this;
    }

    process_options &&set_stdio(std::size_t fd, uv_stdio_container_t entry) && {
      set_stdio(fd, entry);
      return std::move(*this);
    }

  private:
    void add_argument(std::string_view value) {
      arguments.emplace_back(value);
    }

    void add_arguments(std::initializer_list<std::string_view> values) {
      arguments.reserve(arguments.size() + values.size());
      for (auto value : values) {
        add_argument(value);
      }
    }

    void add_environment(std::string_view entry) {
      inherit_parent_environment = false;
      environment.emplace_back(entry);
    }

    void add_environment(std::string_view key, std::string_view value) {
      inherit_parent_environment = false;
      auto entry = std::string{key};
      entry.push_back('=');
      entry.append(value);
      environment.push_back(std::move(entry));
    }

    void set_process_flag(unsigned int flag, bool enable) noexcept {
      if (enable) {
        flags |= flag;
      } else {
        flags &= ~flag;
      }
    }
  };

  class process final : public basic_handle<process, uv_process_t> {
  public:
    using exit_callback = std::function<void(process&, process_exit)>;

    template<auto Callback>
    struct static_callback {};

    process(loop &l, const process_options &options, exit_callback cb) {
      spawn(l.native(), options, std::move(cb), &process::exit_trampoline);
    }

    process(loop_view l, const process_options &options, exit_callback cb) {
      spawn(l.native(), options, std::move(cb), &process::exit_trampoline);
    }

    template<auto Callback>
    process(loop &l, const process_options &options, static_callback<Callback>) {
      spawn(l.native(), options, {}, [](uv_process_t *raw, int64_t exit_status, int term_signal) noexcept {
        detail::invoke_static_callback<Callback>(process::from_native(raw), process_exit{exit_status, term_signal});
      });
    }

    template<auto Callback>
    process(loop_view l, const process_options &options, static_callback<Callback>) {
      spawn(l.native(), options, {}, [](uv_process_t *raw, int64_t exit_status, int term_signal) noexcept {
        detail::invoke_static_callback<Callback>(process::from_native(raw), process_exit{exit_status, term_signal});
      });
    }

    int pid() const noexcept {
      return native()->pid;
    }

    void kill(int signum) {
      throw_if_error(uv_process_kill(native(), signum));
    }

    static void kill(int pid, int signum) {
      throw_if_error(uv_kill(pid, signum));
    }

  private:
    void spawn(uv_loop_t *loop, const process_options &options, exit_callback cb, uv_exit_cb exit_cb) {
      exit_callback_ = std::move(cb);

      std::vector<char *> args;
      args.reserve(options.arguments.size() + 2);
      args.push_back(const_cast<char *>(options.file.c_str()));
      for (const auto &arg : options.arguments) {
        args.push_back(const_cast<char *>(arg.c_str()));
      }
      args.push_back(nullptr);

      std::vector<char *> env;
      if (!options.inherit_parent_environment) {
        env.reserve(options.environment.size() + 1);
        for (const auto &entry : options.environment) {
          env.push_back(const_cast<char *>(entry.c_str()));
        }
        env.push_back(nullptr);
      }

      auto raw_options = uv_process_options_t{};
      raw_options.exit_cb = exit_cb;
      raw_options.file = options.file.c_str();
      raw_options.args = args.data();
      raw_options.env = env.empty() ? nullptr : env.data();
      raw_options.cwd = options.working_directory.empty() ? nullptr : options.working_directory.c_str();
      raw_options.flags = options.flags;
      raw_options.stdio_count = static_cast<int>(options.stdio_entries.size());
      raw_options.stdio = options.stdio_entries.empty() ? nullptr : const_cast<uv_stdio_container_t *>(options.stdio_entries.data());
      raw_options.uid = options.uid;
      raw_options.gid = options.gid;

      throw_if_error(uv_spawn(loop, native(), &raw_options));
    }

    static void exit_trampoline(uv_process_t *raw, int64_t exit_status, int term_signal) noexcept {
      auto &self = process::from_native(raw);
      if (self.exit_callback_) {
        detail::invoke_callback(self.exit_callback_, self, process_exit{exit_status, term_signal});
      }
    }

    exit_callback exit_callback_{};
  };

}
