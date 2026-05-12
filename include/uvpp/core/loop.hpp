#pragma once

#include <system_error>

#include <uv.h>

#include "uvpp/core/error.hpp"

namespace uv {

  class loop_view {
  public:
    explicit loop_view(uv_loop_t *raw) noexcept : raw_{raw} {}

    uv_loop_t *native() const noexcept { return raw_; }

    bool run(uv_run_mode mode = UV_RUN_DEFAULT) {
      return throw_if_error(uv_run(raw_, mode)) != 0;
    }

    void stop() noexcept {
      uv_stop(raw_);
    }

    bool alive() const noexcept {
      return uv_loop_alive(raw_) != 0;
    }

    void update_time() noexcept {
      uv_update_time(raw_);
    }

    uint64_t now() const noexcept {
      return uv_now(raw_);
    }

  private:
    uv_loop_t *raw_;
  };

  class loop {
  public:
    loop() {
      throw_if_error(uv_loop_init(&raw_));
    }

    loop(const loop&) = delete;
    loop& operator=(const loop&) = delete;
    loop(loop&&) = delete;
    loop& operator=(loop&&) = delete;

    ~loop() = default;

    uv_loop_t *native() noexcept { return &raw_; }
    const uv_loop_t *native() const noexcept { return &raw_; }

    loop_view view() noexcept {
      return loop_view{&raw_};
    }

    bool run(uv_run_mode mode = UV_RUN_DEFAULT) {
      return view().run(mode);
    }

    void stop() noexcept {
      uv_stop(&raw_);
    }

    void close() {
      throw_if_error(uv_loop_close(&raw_));
    }

    std::error_code try_close() noexcept {
      return make_error_code(uv_loop_close(&raw_));
    }

    bool alive() const noexcept {
      return uv_loop_alive(const_cast<uv_loop_t *>(&raw_)) != 0;
    }

    void update_time() noexcept {
      uv_update_time(&raw_);
    }

    uint64_t now() const noexcept {
      return uv_now(const_cast<uv_loop_t *>(&raw_));
    }

  private:
    uv_loop_t raw_{};
  };

  inline loop_view default_loop() noexcept {
    return loop_view{uv_default_loop()};
  }

}
