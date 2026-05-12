#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <uv.h>

#include "uvpp/core/callback.hpp"
#include "uvpp/core/error.hpp"
#include "uvpp/core/loop.hpp"
#include "uvpp/handles/handle.hpp"

namespace uv {

  struct process_exit {
    int64_t status = 0;
    int signal = 0;
  };

  struct process_options {
    std::string file;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::string cwd;
    unsigned int flags = 0;
    std::vector<uv_stdio_container_t> stdio;
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
      if (!options.environment.empty()) {
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
      raw_options.cwd = options.cwd.empty() ? nullptr : options.cwd.c_str();
      raw_options.flags = options.flags;
      raw_options.stdio_count = static_cast<int>(options.stdio.size());
      raw_options.stdio = options.stdio.empty() ? nullptr : const_cast<uv_stdio_container_t *>(options.stdio.data());

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
