#pragma once

#include <cassert>
#include <concepts>
#include <coroutine>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "uvpp/co/task.hpp"
#include "uvpp/net/tcp_connection.hpp"

namespace uv::co {

// Experimental resource owner for TCP connections on one loop. It deliberately
// owns no task frames: compose it with task_scope, join those tasks first, then
// co_await finish() to close and release every adopted TCP owner.
class [[nodiscard]] resource_scope {
public:
  explicit resource_scope(uv::loop &execution_loop) noexcept : loop_{&execution_loop} {}

  resource_scope(const resource_scope &) = delete;
  resource_scope &operator=(const resource_scope &) = delete;
  resource_scope(resource_scope &&) = delete;
  resource_scope &operator=(resource_scope &&) = delete;

  ~resource_scope() {
    assert(resources_.empty());
    if (!resources_.empty()) {
      std::terminate();
    }
  }

  // Scope-bound, non-owning capability. Keeping this object or a view after
  // finish() does not retain the connection; its views diagnose subsequent use.
  class [[nodiscard]] tcp_connection_registration {
  public:
    [[nodiscard]] tcp_connection_view view() const noexcept {
      return uv::detail::make_tcp_connection_view(access_);
    }

  private:
    explicit tcp_connection_registration(
        std::shared_ptr<uv::detail::tcp_connection_access> access) noexcept
      : access_{std::move(access)} {}

    std::shared_ptr<uv::detail::tcp_connection_access> access_{};

    friend class resource_scope;
  };

  // Transfers the sole tcp_connection owner into the scope. Reserving before
  // the move gives registration allocation failure a strong rollback: caller
  // ownership has not yet changed.
  [[nodiscard]] tcp_connection_registration own(tcp_connection &&connection) {
    if (finish_started_) {
      throw std::logic_error{"uv::co::resource_scope cannot own after finish"};
    }
    if (!connection.has_execution_loop(*loop_)) {
      throw std::logic_error{"uv::co::resource_scope registered a connection from a different loop"};
    }

    resources_.reserve(resources_.size() + 1);
    auto access = std::make_shared<uv::detail::tcp_connection_access>();
    auto resource = std::make_unique<tcp_resource>(std::move(connection), access);
    access->connection = resource->connection.get();
    resources_.push_back(std::move(resource));
    return tcp_connection_registration{std::move(access)};
  }

  // finish() is deliberately a task rather than a public socket close API. It
  // serializes internal close completion and destroys each owner only after its
  // actual uv_close callback has released all close callback ownership.
  [[nodiscard]] task<void> finish() {
    if (finish_started_) {
      throw std::logic_error{"uv::co::resource_scope can be finished only once"};
    }
    finish_started_ = true;
    return finish_impl();
  }

private:
  struct tcp_resource {
    tcp_resource(tcp_connection &&owned,
        std::shared_ptr<uv::detail::tcp_connection_access> access_token)
      : connection{std::make_unique<tcp_connection>(std::move(owned))},
        access{std::move(access_token)} {}

    std::unique_ptr<tcp_connection> connection{};
    std::shared_ptr<uv::detail::tcp_connection_access> access{};
  };

  class loop_check_awaiter {
  public:
    explicit loop_check_awaiter(resource_scope &scope) noexcept : scope_{scope} {}

    bool await_ready() const noexcept { return false; }

    template<class Promise>
      requires std::derived_from<Promise, detail::task_promise_base>
    bool await_suspend(std::coroutine_handle<Promise> continuation) {
      if (&continuation.promise().execution_loop() != scope_.loop_) {
        throw std::logic_error{"uv::co::resource_scope finished from a different loop"};
      }
      return false;
    }

    void await_resume() const noexcept {}

  private:
    resource_scope &scope_;
  };

  task<void> finish_impl() {
    co_await loop_check_awaiter{*this};
    for (auto &resource : resources_) {
      if (resource->connection->has_active_operation()) {
        throw std::logic_error{
            "uv::co::resource_scope requires task join before TCP cleanup"};
      }
      co_await uv::detail::close_completion(*resource->connection);
      resource->access->connection = nullptr;
      resource->connection.reset();
    }
    resources_.clear();
  }

  uv::loop *loop_ = nullptr;
  std::vector<std::unique_ptr<tcp_resource>> resources_{};
  bool finish_started_ = false;
};

} // namespace uv::co
