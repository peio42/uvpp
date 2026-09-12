#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <coroutine>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "uvpp/co/task.hpp"
#include "uvpp/net/pipe_connection.hpp"
#include "uvpp/net/tcp_connection.hpp"
#include "uvpp/net/tcp_listener.hpp"
#include "uvpp/net/udp_socket.hpp"

namespace uv::co {

// Experimental resource owner for TCP, pipe, and UDP owners on one loop. It
// deliberately owns no task frames: compose it with task_scope, join those tasks
// first, then co_await finish() to close and release every adopted owner.
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

  // Scope-bound listener capability. It retains no listener ownership and is
  // invalidated before finish() destroys the listener owner.
  class [[nodiscard]] tcp_listener_registration {
  public:
    [[nodiscard]] tcp_listener::accept_awaiter accept() const {
      return listener().accept();
    }

  private:
    explicit tcp_listener_registration(
        std::shared_ptr<uv::detail::tcp_listener_access> access) noexcept
      : access_{std::move(access)} {}

    tcp_listener &listener() const {
      if (!access_ || access_->listener == nullptr) {
        throw std::logic_error{"uv::tcp_listener registration used after resource cleanup"};
      }
      return *access_->listener;
    }

    std::shared_ptr<uv::detail::tcp_listener_access> access_{};

    friend class resource_scope;
  };

  class [[nodiscard]] udp_socket_registration {
  public:
    [[nodiscard]] udp_socket_view view() const noexcept {
      return uv::detail::make_udp_socket_view(access_);
    }

  private:
    explicit udp_socket_registration(
        std::shared_ptr<uv::detail::udp_socket_access> access) noexcept
      : access_{std::move(access)} {}

    std::shared_ptr<uv::detail::udp_socket_access> access_{};

    friend class resource_scope;
  };

  class [[nodiscard]] pipe_connection_registration {
  public:
    [[nodiscard]] pipe_connection_view view() const noexcept {
      return uv::detail::make_pipe_connection_view(access_);
    }

  private:
    explicit pipe_connection_registration(
        std::shared_ptr<uv::detail::pipe_connection_access> access) noexcept
      : access_{std::move(access)} {}

    std::shared_ptr<uv::detail::pipe_connection_access> access_{};

    friend class resource_scope;
  };

  // Transfers the sole tcp_connection owner into the scope. Reserving and
  // creating its access token before the move give allocation failure a strong
  // rollback: caller ownership has not yet changed.
  [[nodiscard]] tcp_connection_registration own(tcp_connection &&connection) {
    if (finish_started_) {
      throw std::logic_error{"uv::co::resource_scope cannot own after finish"};
    }
    if (!connection.has_execution_loop(*loop_)) {
      throw std::logic_error{"uv::co::resource_scope registered a connection from a different loop"};
    }

    resources_.reserve(resources_.size() + 1);
    auto access = std::make_shared<uv::detail::tcp_connection_access>();
    auto resource = std::make_unique<tcp_connection_record>(std::move(connection), access);
    access->connection = resource->connection.get();
    resources_.push_back(std::move(resource));
    return tcp_connection_registration{std::move(access)};
  }

  // Transfers the sole tcp_listener owner into the scope. Unlike connection
  // registrations, the returned capability exposes accept() because the server
  // root retains listener control while handlers receive connection views.
  [[nodiscard]] tcp_listener_registration own(tcp_listener &&listener) {
    if (finish_started_) {
      throw std::logic_error{"uv::co::resource_scope cannot own after finish"};
    }
    if (!listener.has_execution_loop(*loop_)) {
      throw std::logic_error{"uv::co::resource_scope registered a listener from a different loop"};
    }

    resources_.reserve(resources_.size() + 1);
    auto access = std::make_shared<uv::detail::tcp_listener_access>();
    auto resource = std::make_unique<tcp_listener_record>(std::move(listener), access);
    access->listener = resource->listener.get();
    resources_.push_back(std::move(resource));
    return tcp_listener_registration{std::move(access)};
  }

  [[nodiscard]] udp_socket_registration own(udp_socket &&socket) {
    if (finish_started_) {
      throw std::logic_error{"uv::co::resource_scope cannot own after finish"};
    }
    if (!socket.has_execution_loop(*loop_)) {
      throw std::logic_error{"uv::co::resource_scope registered a UDP socket from a different loop"};
    }
    resources_.reserve(resources_.size() + 1);
    auto access = std::make_shared<uv::detail::udp_socket_access>();
    auto resource = std::make_unique<udp_socket_record>(std::move(socket), access);
    access->socket = resource->socket.get();
    resources_.push_back(std::move(resource));
    return udp_socket_registration{std::move(access)};
  }

  // Pipe connections follow the same ownership rule as TCP connections: a
  // task receives only a borrowed view and must settle borrowed stream I/O
  // before this scope can start close completion.
  [[nodiscard]] pipe_connection_registration own(pipe_connection &&connection) {
    if (finish_started_) {
      throw std::logic_error{"uv::co::resource_scope cannot own after finish"};
    }
    if (!connection.has_execution_loop(*loop_)) {
      throw std::logic_error{
          "uv::co::resource_scope registered a pipe connection from a different loop"};
    }

    resources_.reserve(resources_.size() + 1);
    auto access = std::make_shared<uv::detail::pipe_connection_access>();
    auto resource = std::make_unique<pipe_connection_record>(std::move(connection), access);
    access->connection = resource->connection.get();
    resources_.push_back(std::move(resource));
    return pipe_connection_registration{std::move(access)};
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
  // Ordering is a scope policy, not a claim that every resource shares one
  // cleanup protocol. A listener first stops admission so it cannot admit new
  // dependents; established TCP and UDP owners close afterwards.
  enum class cleanup_phase {
    stop_admission,
    close_resources,
  };

  class resource_record_base {
  public:
    virtual ~resource_record_base() = default;

    virtual cleanup_phase phase() const noexcept = 0;
    virtual task<void> finish() = 0;
  };

  class tcp_connection_record final : public resource_record_base {
  public:
    tcp_connection_record(tcp_connection &&owned,
        std::shared_ptr<uv::detail::tcp_connection_access> access_token)
      : connection{std::make_unique<tcp_connection>(std::move(owned))},
        access{std::move(access_token)} {}

    cleanup_phase phase() const noexcept override {
      return cleanup_phase::close_resources;
    }

    task<void> finish() override {
      // A task must first settle every borrowing operation and its frame. This
      // record intentionally does not generalize listener accept quiescing to
      // connection read/write, whose borrowed buffer contract remains active.
      if (connection->has_active_operation()) {
        throw std::logic_error{
            "uv::co::resource_scope requires task join before TCP cleanup"};
      }
      co_await uv::detail::close_completion(*connection);
      access->connection = nullptr;
      connection.reset();
    }

    std::unique_ptr<tcp_connection> connection{};

  private:
    std::shared_ptr<uv::detail::tcp_connection_access> access{};
  };

  class tcp_listener_record final : public resource_record_base {
  public:
    tcp_listener_record(tcp_listener &&owned,
        std::shared_ptr<uv::detail::tcp_listener_access> access_token)
      : listener{std::make_unique<tcp_listener>(std::move(owned))},
        access{std::move(access_token)} {}

    cleanup_phase phase() const noexcept override {
      return cleanup_phase::stop_admission;
    }

    task<void> finish() override {
      // The listener close primitive owns its distinct one-shot accept policy:
      // quiesce the accept, release its slots, then await native close.
      co_await uv::detail::close_completion(*listener);
      access->listener = nullptr;
      listener.reset();
    }

    std::unique_ptr<tcp_listener> listener{};

  private:
    std::shared_ptr<uv::detail::tcp_listener_access> access{};
  };

  class pipe_connection_record final : public resource_record_base {
  public:
    pipe_connection_record(pipe_connection &&owned,
        std::shared_ptr<uv::detail::pipe_connection_access> access_token)
      : connection{std::make_unique<pipe_connection>(std::move(owned))},
        access{std::move(access_token)} {}

    cleanup_phase phase() const noexcept override {
      return cleanup_phase::close_resources;
    }

    task<void> finish() override {
      if (connection->has_active_operation()) {
        throw std::logic_error{
            "uv::co::resource_scope requires task join before pipe cleanup"};
      }
      co_await uv::detail::close_completion(*connection);
      access->connection = nullptr;
      connection.reset();
    }

    std::unique_ptr<pipe_connection> connection{};

  private:
    std::shared_ptr<uv::detail::pipe_connection_access> access{};
  };

  class udp_socket_record final : public resource_record_base {
  public:
    udp_socket_record(udp_socket &&owned,
        std::shared_ptr<uv::detail::udp_socket_access> access_token)
      : socket{std::make_unique<udp_socket>(std::move(owned))},
        access{std::move(access_token)} {}

    cleanup_phase phase() const noexcept override { return cleanup_phase::close_resources; }

    task<void> finish() override {
      if (socket->has_active_operation()) {
        throw std::logic_error{
            "uv::co::resource_scope requires task join before UDP cleanup"};
      }
      co_await uv::detail::close_completion(*socket);
      access->socket = nullptr;
      socket.reset();
    }

    std::unique_ptr<udp_socket> socket{};

  private:
    std::shared_ptr<uv::detail::udp_socket_access> access{};
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
    // Current resource cleanup is serial by policy. The records decide their
    // own lifecycle semantics; this loop decides only the phase ordering.
    static constexpr std::array cleanup_order{
        cleanup_phase::stop_admission,
        cleanup_phase::close_resources,
    };
    for (const auto phase : cleanup_order) {
      for (auto &resource : resources_) {
        if (resource->phase() == phase) {
          co_await resource->finish();
        }
      }
    }
    resources_.clear();
  }

  uv::loop *loop_ = nullptr;
  std::vector<std::unique_ptr<resource_record_base>> resources_{};
  bool finish_started_ = false;
};

} // namespace uv::co
