#pragma once

#include <cstddef>
#include <concepts>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/core/version.hpp"
#include "uvpp/co/cancellation.hpp"
#include "uvpp/co/task.hpp"
#include "uvpp/detail/async_close_state.hpp"
#include "uvpp/detail/owner_close.hpp"
#include "uvpp/handles/handle.hpp"
#include "uvpp/handles/stream.hpp"

namespace uv::raw {

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

#if UVPP_HAS_PROCESS_WINDOWS_HIDE_CONSOLE_GUI
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
#endif

#if UVPP_HAS_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME
    process_options &windows_file_path_exact_name(bool enable = true) & noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME, enable);
      return *this;
    }

    process_options &&windows_file_path_exact_name(bool enable = true) && noexcept {
      set_process_flag(UV_PROCESS_WINDOWS_FILE_PATH_EXACT_NAME, enable);
      return std::move(*this);
    }
#endif

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

  class process final {
  public:
    using raw_type = uv_process_t;
    using exit_callback = std::function<void(process&, process_exit)>;
    using close_callback = std::function<void(process&)>;

    template<auto Callback>
    struct static_callback {};

    process(loop &l, const process_options &options, exit_callback cb)
      : storage_{std::make_unique<storage>(this)} {
      spawn(l.native(), options, std::move(cb), &process::exit_trampoline);
    }

    process(loop_view l, const process_options &options, exit_callback cb)
      : storage_{std::make_unique<storage>(this)} {
      spawn(l.native(), options, std::move(cb), &process::exit_trampoline);
    }

    template<auto Callback>
    process(loop &l, const process_options &options, static_callback<Callback>)
      : storage_{std::make_unique<storage>(this)} {
      spawn(l.native(), options, {}, [](uv_process_t *raw, int64_t exit_status, int term_signal) noexcept {
        detail::invoke_static_callback<Callback>(process::from_native(raw), process_exit{exit_status, term_signal});
      });
    }

    template<auto Callback>
    process(loop_view l, const process_options &options, static_callback<Callback>)
      : storage_{std::make_unique<storage>(this)} {
      spawn(l.native(), options, {}, [](uv_process_t *raw, int64_t exit_status, int term_signal) noexcept {
        detail::invoke_static_callback<Callback>(process::from_native(raw), process_exit{exit_status, term_signal});
      });
    }

    process(const process &) = delete;
    process &operator=(const process &) = delete;
    process(process &&) = delete;
    process &operator=(process &&) = delete;

    uv_process_t *native() noexcept { return storage_->native(); }
    const uv_process_t *native() const noexcept { return storage_->native(); }

    uv_handle_t *native_handle() noexcept {
      return reinterpret_cast<uv_handle_t *>(native());
    }

    const uv_handle_t *native_handle() const noexcept {
      return reinterpret_cast<const uv_handle_t *>(native());
    }

    handle_view view() noexcept { return handle_view{native_handle()}; }

    static process &from_native(uv_process_t *raw) noexcept {
      return *storage::from_native(raw).owner;
    }

    static const process &from_native(const uv_process_t *raw) noexcept {
      return *storage::from_native(raw).owner;
    }

    static process &from_native(uv_handle_t *raw) noexcept {
      return from_native(reinterpret_cast<uv_process_t *>(raw));
    }

    static const process &from_native(const uv_handle_t *raw) noexcept {
      return from_native(reinterpret_cast<const uv_process_t *>(raw));
    }

    template<class T>
    void user_data(T *data) noexcept {
      native_handle()->data = data;
    }

    template<class T>
    void user_data(T &data) noexcept {
      user_data(&data);
    }

    template<class T>
    T *user_data() noexcept {
      return static_cast<T *>(native_handle()->data);
    }

    template<class T>
    const T *user_data() const noexcept {
      return static_cast<const T *>(native_handle()->data);
    }

    void clear_user_data() noexcept {
      native_handle()->data = nullptr;
    }

    bool active() const noexcept {
      return uv_is_active(const_cast<uv_handle_t *>(native_handle())) != 0;
    }

    bool closing() const noexcept {
      return uv_is_closing(const_cast<uv_handle_t *>(native_handle())) != 0;
    }

    void close() noexcept {
      uv_close(native_handle(), nullptr);
    }

    void close(close_callback callback) noexcept {
      close_callback_ = std::move(callback);
      uv_close(native_handle(), &process::close_trampoline);
    }

    template<auto Callback>
    void close_static() noexcept {
      uv_close(native_handle(), [](uv_handle_t *raw) noexcept {
        detail::invoke_static_callback<Callback>(process::from_native(raw));
      });
    }

    void ref() noexcept {
      uv_ref(native_handle());
    }

    void unref() noexcept {
      uv_unref(native_handle());
    }

    bool has_ref() const noexcept {
      return uv_has_ref(const_cast<uv_handle_t *>(native_handle())) != 0;
    }

    int send_buffer_size() const {
      int value = 0;
      throw_if_error(uv_send_buffer_size(const_cast<uv_handle_t *>(native_handle()), &value));
      return value;
    }

    void send_buffer_size(int value) {
      throw_if_error(uv_send_buffer_size(native_handle(), &value));
    }

    int receive_buffer_size() const {
      int value = 0;
      throw_if_error(uv_recv_buffer_size(const_cast<uv_handle_t *>(native_handle()), &value));
      return value;
    }

    void receive_buffer_size(int value) {
      throw_if_error(uv_recv_buffer_size(native_handle(), &value));
    }

    uv_os_fd_t fileno() const {
      uv_os_fd_t fd{};
      throw_if_error(uv_fileno(const_cast<uv_handle_t *>(native_handle()), &fd));
      return fd;
    }

    uv_pid_t pid() const noexcept {
#if UVPP_HAS_PROCESS_GET_PID
      return uv_process_get_pid(native());
#else
      return native()->pid;
#endif
    }

    void kill(int signum) {
      throw_if_error(uv_process_kill(native(), signum));
    }

    static void kill(uv_pid_t pid, int signum) {
      throw_if_error(uv_kill(static_cast<int>(pid), signum));
    }

    static void disable_stdio_inheritance() noexcept {
      uv_disable_stdio_inheritance();
    }

  private:
    struct storage;
    using storage_base = detail::native_storage<storage, uv_process_t, uv_handle_t>;

    struct storage : storage_base {

      explicit storage(process *owner) noexcept : owner{owner} {}

      process *owner = nullptr;
      exit_callback exit_callback_{};
    };

    static_assert(std::is_standard_layout_v<storage_base>);

    void spawn(uv_loop_t *loop, const process_options &options, exit_callback cb, uv_exit_cb exit_cb) {
      storage_->exit_callback_ = std::move(cb);

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

      auto status = uv_spawn(loop, native(), &raw_options);
      if (status >= 0) {
        return;
      }

      // uv_spawn() can initialize and attach the process handle before a later
      // spawn step fails. The constructor will now unwind, so transfer this
      // storage to the close callback instead of leaving the loop with a
      // pointer into the destroyed process object.
      if (native_handle()->loop != nullptr) {
        auto failed_storage = std::move(storage_);
        failed_storage->owner = nullptr;
        uv_close(failed_storage->native_base(),
                 &process::failed_spawn_close_trampoline);
        failed_storage.release();
      }

      throw error(status);
    }

    static void exit_trampoline(uv_process_t *raw, int64_t exit_status, int term_signal) noexcept {
      auto &state = storage::from_native(raw);
      if (state.exit_callback_) {
        detail::invoke_callback(state.exit_callback_, *state.owner, process_exit{exit_status, term_signal});
      }
    }

    static void close_trampoline(uv_handle_t *raw) noexcept {
      auto &self = process::from_native(raw);
      auto callback = std::move(self.close_callback_);
      if (callback) {
        detail::invoke_callback(callback, self);
      }
    }

    static void failed_spawn_close_trampoline(uv_handle_t *raw) noexcept {
      delete &storage::from_native(reinterpret_cast<uv_process_t *>(raw));
    }

    std::unique_ptr<storage> storage_{};
    close_callback close_callback_{};
  };

} // namespace uv::raw

namespace uv {

// Minimal v3 process launch configuration. The argument vector excludes argv[0]:
// spawn prepends file automatically. Stdio, environment and platform flags stay
// on uv::raw::process_options until their ownership contracts are designed.
struct process_options {
  std::string file;
  std::vector<std::string> arguments;
  std::optional<std::string> cwd;
};

struct process_exit {
  std::int64_t status = 0;
  int signal = 0;
};

class process;

namespace ops {
struct process_ops_access;
}

namespace detail {

struct process_state {
  uv_process_t native{};
  uv::loop *loop = nullptr;
  async_close_state close{};
  void *waiter = nullptr;
  void (*deliver_waiter)(void *, int, process_exit) noexcept = nullptr;
  process_exit exit{};
  bool exited = false;
  // Distinct from async_close_state ownership: process owner release may
  // precede the legal start of native close.
  bool owner_released = false;

  static process_state &from_handle(uv_process_t *raw) noexcept {
    auto *bytes = reinterpret_cast<char *>(raw);
    return *reinterpret_cast<process_state *>(bytes - offsetof(process_state, native));
  }

  bool closing() const noexcept { return close.closing(); }
  bool has_active_operation() const noexcept { return !exited; }

  bool claim_waiter(void *context,
                    void (*deliver)(void *, int, process_exit) noexcept) noexcept {
    if (waiter != nullptr) {
      return false;
    }
    waiter = context;
    deliver_waiter = deliver;
    return true;
  }

  bool release_waiter(void *context) noexcept {
    if (waiter != context) {
      return false;
    }
    waiter = nullptr;
    deliver_waiter = nullptr;
    return true;
  }

  void cancel_waiter() noexcept {
    auto *context = std::exchange(waiter, nullptr);
    auto deliver = std::exchange(deliver_waiter, nullptr);
    if (deliver != nullptr) {
      deliver(context, UV_ECANCELED, {});
    }
  }

  bool request_close() noexcept {
    if (!exited || !close.begin()) {
      return false;
    }
    uv_close(reinterpret_cast<uv_handle_t *>(&native), &process_state::on_close);
    return true;
  }

  void release_owner() noexcept {
    owner_released = true;
    // async_close_state owns deletion after an already-started close. This
    // notification is also safe before exit; process_state::owner_released
    // separately records that native close must be deferred until then.
    close.owner_released();
    if (!exited) {
      return;
    }
    if (close.open()) {
      (void)request_close();
    } else if (close.closed()) {
      delete this;
    }
  }

  static void on_exit(uv_process_t *raw, std::int64_t status, int signal) noexcept {
    auto &self = from_handle(raw);
    self.exit = {status, signal};
    self.exited = true;

    // If no C++ owner remains, close before delivering a coroutine waiter. The
    // waiter may destroy its frame when resumed, but native storage remains
    // pinned by the outstanding uv_close callback.
    if (self.owner_released) {
      (void)self.request_close();
    }

    auto *context = std::exchange(self.waiter, nullptr);
    auto deliver = std::exchange(self.deliver_waiter, nullptr);
    if (deliver != nullptr) {
      deliver(context, 0, self.exit);
    }
  }

  static void on_close(uv_handle_t *raw) noexcept {
    auto &self = from_handle(reinterpret_cast<uv_process_t *>(raw));
    self.close.complete([&self]() noexcept {
      if (self.owner_released) {
        delete &self;
      }
    });
  }
};

static_assert(std::is_standard_layout_v<process_state>);

using process_close_completion = owner_close_awaiter<process_state, false>;
using process_close_result = owner_close_awaiter<process_state, true>;
[[nodiscard]] process_close_completion close_completion(process &) noexcept;
[[nodiscard]] process_close_result close_result(process &) noexcept;

} // namespace detail

class process {
public:
  process(const process &) = delete;
  process &operator=(const process &) = delete;

  process(process &&other) noexcept : state_{std::move(other.state_)} {}

  process &operator=(process &&other) noexcept {
    if (this != &other) {
      reset();
      state_ = std::move(other.state_);
    }
    return *this;
  }

  process(uv::loop &loop, const process_options &options) {
    auto state = std::make_unique<detail::process_state>();
    state->loop = &loop;

    std::vector<char *> arguments;
    arguments.reserve(options.arguments.size() + 2);
    arguments.push_back(const_cast<char *>(options.file.c_str()));
    for (const auto &argument : options.arguments) {
      arguments.push_back(const_cast<char *>(argument.c_str()));
    }
    arguments.push_back(nullptr);

    uv_process_options_t native_options{};
    native_options.exit_cb = &detail::process_state::on_exit;
    native_options.file = options.file.c_str();
    native_options.args = arguments.data();
    native_options.cwd = options.cwd ? options.cwd->c_str() : nullptr;

    const int status = uv_spawn(loop.native(), &state->native, &native_options);
    if (status >= 0) {
      state_ = std::move(state);
      return;
    }

    // Unlike ordinary libuv initializers, uv_spawn() requires uv_close() for
    // every result, including a failed spawn. No C++ owner exists during
    // constructor unwind, so the native close callback owns this state.
    state->release_owner();
    uv_close(reinterpret_cast<uv_handle_t *>(&state->native),
             &detail::process_state::on_close);
    (void)state.release();
    throw_if_error(status);
  }

  ~process() { reset(); }

  uv_process_t *native() noexcept { return state_ ? &state_->native : nullptr; }
  const uv_process_t *native() const noexcept { return state_ ? &state_->native : nullptr; }
  uv_handle_t *native_handle() noexcept { return reinterpret_cast<uv_handle_t *>(native()); }
  const uv_handle_t *native_handle() const noexcept {
    return reinterpret_cast<const uv_handle_t *>(native());
  }
  uv_pid_t pid() const noexcept {
#if UVPP_HAS_PROCESS_GET_PID
    return state_ ? uv_process_get_pid(native()) : 0;
#else
    return state_ ? native()->pid : 0;
#endif
  }
  bool exited() const noexcept { return state_ != nullptr && state_->exited; }
  bool closing() const noexcept { return state_ != nullptr && state_->closing(); }
  bool has_execution_loop(const uv::loop &execution_loop) const noexcept {
    return state_ != nullptr && state_->loop == &execution_loop;
  }

  void kill(int signum) {
    if (state_ == nullptr || state_->closing()) {
      throw_if_error(UV_EBADF);
    }
    throw_if_error(uv_process_kill(&state_->native, signum));
  }

  template<bool ExplicitResult>
  class wait_awaiter {
  public:
    explicit wait_awaiter(detail::process_state *state) noexcept : state_{state} {}
    wait_awaiter(const wait_awaiter &) = delete;
    wait_awaiter &operator=(const wait_awaiter &) = delete;
    wait_awaiter(wait_awaiter &&) = delete;
    wait_awaiter &operator=(wait_awaiter &&) = delete;

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, co::detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (state_ == nullptr || state_->closing()) {
        status_ = UV_EBADF;
        return false;
      }
      if (&continuation.promise().execution_loop() != state_->loop) {
        throw std::logic_error{"uv::process wait used from a different loop"};
      }
      if (continuation.promise().stop_requested()) {
        status_ = UV_ECANCELED;
        return false;
      }
      if (state_->exited) {
        exit_ = state_->exit;
        return false;
      }
      if (!state_->claim_waiter(this, &wait_awaiter::on_delivery)) {
        status_ = UV_EBUSY;
        return false;
      }
      continuation_ = continuation;
      cancellation_ = continuation.promise().cancellation();
      if (cancellation_ != nullptr && !cancellation_->register_callback(
          cancellation_registration_, &wait_awaiter::on_stop_requested, this)) {
        (void)state_->release_waiter(this);
        continuation_ = {};
        cancellation_ = nullptr;
        status_ = UV_ECANCELED;
        return false;
      }
      return true;
    }

    auto await_resume() {
      if constexpr (ExplicitResult) {
        if (status_ < 0) {
          return uv::result<process_exit>{uv::make_error_code(status_)};
        }
        return uv::result<process_exit>{exit_};
      } else {
        throw_if_error(status_);
        return exit_;
      }
    }

  private:
    static void on_delivery(void *context, int status, process_exit exit) noexcept {
      auto &self = *static_cast<wait_awaiter *>(context);
      if (self.cancellation_ != nullptr) {
        self.cancellation_->unregister(self.cancellation_registration_);
        self.cancellation_ = nullptr;
      }
      auto continuation = std::exchange(self.continuation_, {});
      self.status_ = status;
      self.exit_ = exit;
      continuation.resume();
    }

    static void on_stop_requested(void *context) noexcept {
      auto &self = *static_cast<wait_awaiter *>(context);
      if (self.state_ != nullptr) {
        self.state_->cancel_waiter();
      }
    }

    detail::process_state *state_ = nullptr;
    std::coroutine_handle<> continuation_{};
    co::detail::cancellation_state *cancellation_ = nullptr;
    co::detail::cancellation_registration cancellation_registration_{};
    process_exit exit_{};
    int status_ = 0;
  };

  [[nodiscard]] wait_awaiter<false> wait() noexcept { return wait_awaiter<false>{state_.get()}; }
  [[nodiscard]] detail::process_close_completion close() & noexcept {
    return detail::process_close_completion{state_.get(), true};
  }
  detail::process_close_completion close() && = delete;

  void request_close() {
    if (state_ == nullptr) {
      throw_if_error(UV_EBADF);
    }
    if (!state_->exited) {
      throw_if_error(UV_EBUSY);
    }
    (void)state_->request_close();
  }

private:
  void reset() noexcept {
    if (!state_) {
      return;
    }
    auto *state = state_.release();
    state->release_owner();
  }

  std::unique_ptr<detail::process_state> state_{};

  friend detail::process_close_completion detail::close_completion(process &) noexcept;
  friend detail::process_close_result detail::close_result(process &) noexcept;
  friend struct ops::process_ops_access;
};

namespace detail {

[[nodiscard]] inline process_close_completion close_completion(process &child) noexcept {
  return process_close_completion{child.state_.get(), true};
}

[[nodiscard]] inline process_close_result close_result(process &child) noexcept {
  return process_close_result{child.state_.get(), true};
}

} // namespace detail

namespace ops {

struct process_ops_access {
  static process::wait_awaiter<true> wait(process &child) noexcept {
    return process::wait_awaiter<true>{child.state_.get()};
  }
  static status kill(process &child, int signum) noexcept {
    if (child.state_ == nullptr || child.state_->closing()) {
      return status::from_native(UV_EBADF);
    }
    return status::from_native(uv_process_kill(&child.state_->native, signum));
  }
};

[[nodiscard]] inline process::wait_awaiter<true> wait(process &child) noexcept {
  return process_ops_access::wait(child);
}

[[nodiscard]] inline status kill(process &child, int signum) noexcept {
  return process_ops_access::kill(child, signum);
}

[[nodiscard]] inline detail::process_close_result close(process &child) noexcept {
  return detail::close_result(child);
}

} // namespace ops

} // namespace uv
