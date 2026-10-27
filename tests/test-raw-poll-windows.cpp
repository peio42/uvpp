#ifdef _WIN32

#include <atomic>
#include <bit>
#include <stdexcept>

#include "gtest/gtest.h"
#include "uvpp/handles/poll.hpp"
#include "uvpp/handles/timer.hpp"
#include <windows.h>

namespace {

using getsockopt_function = decltype(&::getsockopt);

getsockopt_function system_getsockopt() noexcept {
  static const auto function = std::bit_cast<getsockopt_function>(
    GetProcAddress(GetModuleHandleW(L"ws2_32.dll"), "getsockopt"));
  return function;
}

// Only the installing thread accesses the non-atomic fields. Other threads
// immediately delegate to Winsock; the state itself has process lifetime.
struct failure_state {
  std::atomic<DWORD> thread{0};
  uv_loop_t *loop = nullptr;
  uv_handle_t *expected = nullptr;
  int calls = 0;
  bool registered = false;
  bool active = false;
  bool correct_loop = false;
};
failure_state failure;

void observe_registration(uv_handle_t *handle, void*) noexcept {
  if (handle->type != UV_POLL || (failure.expected && failure.expected != handle)) return;
  failure.registered = true;
  failure.active = uv_is_active(handle) != 0;
  failure.correct_loop = handle->loop == failure.loop;
}

int WSAAPI fail_protocol_query(SOCKET socket, int level, int option, char *value, int *length) noexcept {
  if (failure.thread.load() == GetCurrentThreadId() &&
      level == SOL_SOCKET && option == SO_PROTOCOL_INFOW) {
    ++failure.calls;
    uv_walk(failure.loop, observe_registration, nullptr);
    WSASetLastError(WSAEINVAL);
    return SOCKET_ERROR;
  }
  return system_getsockopt()(socket, level, option, value, length);
}

// Patch the getsockopt import in the module which actually contains libuv.
// This exercises the installed native binary (DLL or static executable), not
// a recompiled copy of poll.c or a substituted uv_poll_init_socket function.
class scoped_protocol_failure {
public:
  explicit scoped_protocol_failure(uv::loop &loop, uv_handle_t *expected = nullptr) {
    if (failure.thread.load() != 0 || system_getsockopt() == nullptr)
      throw std::runtime_error{"Winsock fault injector unavailable"};
    HMODULE module = nullptr;
    const auto address = std::bit_cast<ULONG_PTR>(&uv_poll_init_socket);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module))
      throw std::runtime_error{"Cannot locate native libuv module"};
    auto *base = reinterpret_cast<unsigned char *>(module);
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0) throw std::runtime_error{"No native import table"};
    auto *descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + imports.VirtualAddress);
    const auto original = std::bit_cast<ULONG_PTR>(system_getsockopt());
    for (; descriptor->Name != 0 && slot_ == nullptr; ++descriptor) {
      auto *thunk = reinterpret_cast<IMAGE_THUNK_DATA *>(base + descriptor->FirstThunk);
      for (; thunk->u1.Function != 0; ++thunk) {
        if (thunk->u1.Function == original) {
          slot_ = reinterpret_cast<void **>(&thunk->u1.Function);
          break;
        }
      }
    }
    if (slot_ == nullptr) throw std::runtime_error{"Native getsockopt import not found"};
    DWORD protection = 0;
    if (!VirtualProtect(slot_, sizeof(void*), PAGE_READWRITE, &protection))
      throw std::runtime_error{"Cannot write native getsockopt import"};
    failure.loop = loop.native();
    failure.expected = expected;
    failure.calls = 0;
    failure.registered = failure.active = failure.correct_loop = false;
    failure.thread.store(GetCurrentThreadId());
    previous_ = InterlockedExchangePointer(slot_, std::bit_cast<void *>(&fail_protocol_query));
    DWORD ignored = 0;
    if (!VirtualProtect(slot_, sizeof(void*), protection, &ignored)) {
      InterlockedExchangePointer(slot_, previous_);
      failure.thread.store(0);
      throw std::runtime_error{"Cannot restore native import protection"};
    }
  }

  scoped_protocol_failure(const scoped_protocol_failure&) = delete;
  scoped_protocol_failure &operator=(const scoped_protocol_failure&) = delete;

  ~scoped_protocol_failure() {
    DWORD protection = 0;
    if (!VirtualProtect(slot_, sizeof(void*), PAGE_READWRITE, &protection)) std::terminate();
    InterlockedExchangePointer(slot_, previous_);
    DWORD ignored = 0;
    if (!VirtualProtect(slot_, sizeof(void*), protection, &ignored)) std::terminate();
    failure.thread.store(0);
  }

  void expect_native_failure_after_registration() const {
    EXPECT_EQ(failure.calls, 1);
    EXPECT_TRUE(failure.registered);
    EXPECT_FALSE(failure.active);
    EXPECT_TRUE(failure.correct_loop);
  }

private:
  void **slot_ = nullptr;
  void *previous_ = nullptr;
};

struct socket_pair {
  uv_os_sock_t sockets[2]{INVALID_SOCKET, INVALID_SOCKET};
  socket_pair() {
    if (uv_socketpair(SOCK_STREAM, 0, sockets, 0, 0) != 0)
      throw std::runtime_error{"uv_socketpair failed"};
  }
  ~socket_pair() {
    for (auto socket : sockets) closesocket(socket);
  }
};

class RawPollWindowsInit : public ::testing::Test {
protected:
  uv::loop loop;
  socket_pair sockets;
  uv::raw::timer neighbor;
  uv::raw::poll watcher;

  void SetUp() override {
    RecordProperty("libuv_runtime", uv_version_string());
    RecordProperty("libuv_headers", UV_VERSION_HEX);
    EXPECT_EQ(uv_version(), static_cast<unsigned int>(UV_VERSION_HEX));
  }

  ~RawPollWindowsInit() override {
    if (watcher.initialized() && !watcher.closing()) watcher.close();
    if (neighbor.initialized() && !neighbor.closing()) neighbor.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
  }
};

TEST_F(RawPollWindowsInit, nativeProtocolFailureIsObservedBeforeWrapperRollbackAndRetry) {
  ASSERT_TRUE(neighbor.init(loop));
  int marker = 42;
  watcher.user_data(marker);
  auto *native = watcher.native();
  {
    scoped_protocol_failure injection{loop, watcher.native_handle()};
    const auto result = watcher.init(loop.view(), sockets.sockets[0], uv::socket_poll);
    EXPECT_EQ(result.error(), uv::make_error_code(uv_translate_sys_error(WSAEINVAL)));
    injection.expect_native_failure_after_registration();
    EXPECT_FALSE(watcher.initialized());
    EXPECT_EQ(watcher.native(), native);
    EXPECT_EQ(watcher.user_data<int>(), &marker);
    auto handles = loop.handles();
    EXPECT_EQ(handles.size(), 1u);
    if (!handles.empty()) EXPECT_EQ(handles.front().native_handle(), neighbor.native_handle());
    EXPECT_FALSE(loop.alive());
  }
  // The import is restored: retry invokes the real Winsock protocol query.
  EXPECT_TRUE(watcher.init(loop, sockets.sockets[0], uv::socket_poll));
  neighbor.close();
}

void construct_with_owned_loop(uv::loop &loop, uv_os_sock_t socket) {
  uv::raw::poll value{loop, socket, uv::socket_poll};
  value.close();
  loop.run();
}
void construct_with_loop_view(uv::loop &loop, uv_os_sock_t socket) {
  uv::raw::poll value{loop.view(), socket, uv::socket_poll};
  value.close();
  loop.run();
}

TEST_F(RawPollWindowsInit, throwingConstructorsRemoveNativeLateFailureRegistration) {
  {
    scoped_protocol_failure injection{loop};
    EXPECT_THROW(construct_with_owned_loop(loop, sockets.sockets[0]), uv::error);
    injection.expect_native_failure_after_registration();
    EXPECT_TRUE(loop.handles().empty());
  }
  {
    scoped_protocol_failure injection{loop};
    EXPECT_THROW(construct_with_loop_view(loop, sockets.sockets[0]), uv::error);
    injection.expect_native_failure_after_registration();
    EXPECT_TRUE(loop.handles().empty());
  }
  EXPECT_FALSE(loop.alive());
  EXPECT_TRUE(watcher.init(loop, sockets.sockets[0], uv::socket_poll));
}

} // namespace
#endif
