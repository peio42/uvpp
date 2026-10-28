#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include "platform-support.hpp"
#include "gtest/gtest.h"
#include "uvpp/handles/tty.hpp"

#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#ifdef __APPLE__
#include <sys/resource.h>
#endif
#include <unistd.h>
#else
#include <io.h>
#endif

namespace {
using tty = uv::raw::tty;

#ifdef _WIN32
class windows_handle {
public:
  explicit windows_handle(HANDLE value = nullptr) noexcept : value_{value} {}
  ~windows_handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
  windows_handle(const windows_handle&) = delete;
  windows_handle &operator=(const windows_handle&) = delete;
  HANDLE get() const noexcept { return value_; }
private:
  HANDLE value_;
};

bool is_console_child() noexcept {
  const wchar_t *command = GetCommandLineW();
  return command && std::wstring_view{command}.find(L"--uvpp-tty-console-child") != std::wstring_view::npos;
}

void run_console_child(const wchar_t *test_name) {
  // CREATE_NEW_CONSOLE isolates console modes and input as well as AllocConsole
  // from the parent. Redirect diagnostics so child GTest failures remain visible.
  const auto input_cp = GetConsoleCP();
  const auto output_cp = GetConsoleOutputCP();
  const auto window = GetConsoleWindow();
  const auto std_input = GetStdHandle(STD_INPUT_HANDLE);
  const auto std_output = GetStdHandle(STD_OUTPUT_HANDLE);
  const auto std_error = GetStdHandle(STD_ERROR_HANDLE);
  DWORD before_input_mode = 0, before_output_mode = 0;
  const bool has_input_mode = GetConsoleMode(std_input, &before_input_mode) != 0;
  const bool has_output_mode = GetConsoleMode(std_output, &before_output_mode) != 0;

  SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE raw_read = nullptr, raw_write = nullptr;
  ASSERT_TRUE(CreatePipe(&raw_read, &raw_write, &attributes, 0));
  windows_handle read_pipe{raw_read};
  windows_handle write_pipe{raw_write};
  ASSERT_TRUE(SetHandleInformation(read_pipe.get(), HANDLE_FLAG_INHERIT, 0));
  windows_handle input{CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  ASSERT_NE(input.get(), INVALID_HANDLE_VALUE);
  std::array<wchar_t, 32768> executable{};
  const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  ASSERT_GT(length, 0u);
  ASSERT_LT(length, executable.size());
  std::wstring command = L"\"" + std::wstring{executable.data(), length} + L"\" --gtest_filter=" +
                         test_name + L" --gtest_brief=1 --uvpp-tty-console-child";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input.get();
  startup.hStdOutput = write_pipe.get();
  startup.hStdError = write_pipe.get();
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE,
                                      CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process);
  ASSERT_TRUE(created) << "CreateProcessW failed: " << GetLastError();
  windows_handle child{process.hProcess};
  windows_handle thread{process.hThread};

  std::string output;
  auto drain = [&] {
    DWORD available = 0;
    while (PeekNamedPipe(read_pipe.get(), nullptr, 0, nullptr, &available, nullptr) && available) {
      std::array<char, 4096> buffer{};
      DWORD count = 0;
      const DWORD size = (std::min)(available, static_cast<DWORD>(buffer.size()));
      if (!ReadFile(read_pipe.get(), buffer.data(), size, &count, nullptr) || !count) break;
      output.append(buffer.data(), count);
    }
  };
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
  DWORD wait = WAIT_TIMEOUT;
  while (wait == WAIT_TIMEOUT && std::chrono::steady_clock::now() < deadline) {
    drain();
    wait = WaitForSingleObject(child.get(), 10);
  }
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(child.get(), 1);
    WaitForSingleObject(child.get(), 5000);
  }
  drain();
  DWORD exit_code = 0;
  EXPECT_EQ(wait, WAIT_OBJECT_0) << output;
  ASSERT_TRUE(GetExitCodeProcess(child.get(), &exit_code));
  EXPECT_EQ(exit_code, 0u) << output;
  EXPECT_EQ(GetConsoleCP(), input_cp);
  EXPECT_EQ(GetConsoleOutputCP(), output_cp);
  EXPECT_EQ(GetConsoleWindow(), window);
  EXPECT_EQ(GetStdHandle(STD_INPUT_HANDLE), std_input);
  EXPECT_EQ(GetStdHandle(STD_OUTPUT_HANDLE), std_output);
  EXPECT_EQ(GetStdHandle(STD_ERROR_HANDLE), std_error);
  DWORD after_mode = 0;
  const bool still_has_input_mode = GetConsoleMode(std_input, &after_mode) != 0;
  EXPECT_EQ(still_has_input_mode, has_input_mode);
  if (has_input_mode && still_has_input_mode) { EXPECT_EQ(after_mode, before_input_mode); }
  const bool still_has_output_mode = GetConsoleMode(std_output, &after_mode) != 0;
  EXPECT_EQ(still_has_output_mode, has_output_mode);
  if (has_output_mode && still_has_output_mode) { EXPECT_EQ(after_mode, before_output_mode); }
}

int open_console(const char *name) {
  HANDLE handle = CreateFileA(name, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    if (!AllocConsole() && GetLastError() != ERROR_ACCESS_DENIED) return -1;
    handle = CreateFileA(name, GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_EXISTING, 0, nullptr);
  }
  if (handle == INVALID_HANDLE_VALUE) return -1;
  const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDWR | _O_BINARY);
  if (fd < 0) CloseHandle(handle);
  return fd;
}
#else
struct pty_pair {
  int master = -1;
  int slave = -1;
  bool open() {
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) return false;
    const char *name = ptsname(master);
    if (!name) return false;
    slave = ::open(name, O_RDWR | O_NOCTTY);
    return slave >= 0;
  }
  ~pty_pair() {
    if (slave >= 0) ::close(slave);
    if (master >= 0) ::close(master);
  }
  void after_init(const tty &handle) {
    // On Unix libuv may reopen the slave and own the reopened descriptor;
    // on fallback it owns the descriptor supplied by the caller instead.
    if (handle.fileno().value() == slave) slave = -1;
  }
};
#endif

TEST(RawTty, defaultStateAndNativeInitFailureAreRetryable) {
#ifdef _WIN32
  if (!is_console_child()) {
    run_console_child(L"RawTty.defaultStateAndNativeInitFailureAreRetryable");
    return;
  }
#endif
  static_assert(std::is_nothrow_default_constructible_v<tty>);
  static_assert(!std::is_copy_constructible_v<tty> && !std::is_move_constructible_v<tty>);
  static_assert(!std::is_convertible_v<tty&, uv_tty_t*>);
  static_assert(uv::stream_handle<tty>);
  static_assert(std::is_same_v<decltype(std::declval<const tty&>().size()), uv::result<uv::terminal_size>>);
  static_assert(noexcept(std::declval<tty&>().set_mode(uv::tty_mode::raw)));
  uv::loop loop;
  tty handle;
  int marker = 42;
  handle.user_data(marker);
  auto *native = handle.native();
  EXPECT_FALSE(handle.initialized());
  EXPECT_FALSE(handle.active()); EXPECT_FALSE(handle.closing()); EXPECT_FALSE(handle.has_ref());
  EXPECT_EQ(handle.size().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(handle.set_mode(uv::tty_mode::raw).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(handle.read_stop().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(handle.fileno().error(), uv::make_error_code(UV_EBADF));
  handle.close();
  EXPECT_TRUE(loop.handles().empty());
  uv_tty_t native_reference{};
  const int expected = uv_tty_init(loop.native(), &native_reference, -1, 0);
  ASSERT_LT(expected, 0);
  EXPECT_EQ(handle.init(loop, -1, false).error(), uv::make_error_code(expected));
  EXPECT_FALSE(handle.initialized());
  EXPECT_EQ(handle.native(), native);
  EXPECT_EQ(handle.user_data<int>(), &marker);
  EXPECT_TRUE(loop.handles().empty());
  EXPECT_THROW((tty{loop, -1, false}), uv::error);
#ifndef _WIN32
  pty_pair terminals;
  ASSERT_TRUE(terminals.open());
  ASSERT_TRUE(handle.init(loop.view(), terminals.slave, true));
  terminals.after_init(handle);
#else
  const int fd = open_console("CONOUT$");
  ASSERT_GE(fd, 0);
  const auto initialized = handle.init(loop.view(), fd, false);
  if (!initialized) _close(fd);
  ASSERT_TRUE(initialized);
#endif
  EXPECT_TRUE(handle.initialized());
  EXPECT_EQ(handle.native(), native);
  EXPECT_EQ(handle.user_data<int>(), &marker);
  EXPECT_EQ(handle.init(loop, -1, false).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(&tty::from_native(handle.native()), &handle);
  EXPECT_EQ(&tty::from_native(handle.native_handle()), &handle);
  bool closed = false;
  handle.close([&](tty &self) { EXPECT_TRUE(self.initialized()); closed = true; });
  EXPECT_TRUE(handle.closing());
  EXPECT_EQ(handle.size().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(handle.set_mode(uv::tty_mode::normal).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(handle.init(loop, -1, false).error(), uv::make_error_code(UV_EALREADY));
  handle.close();
  loop.run();
  EXPECT_TRUE(closed);
#ifdef _WIN32
  if (fd <= 2) _close(fd); // libuv duplicates and retains standard descriptors.
#endif
  EXPECT_TRUE(handle.initialized());
  EXPECT_NO_THROW(loop.close());
}

TEST(RawTty, globalTerminalOperationsDoNotNeedAHandle) {
#ifdef _WIN32
  if (!is_console_child()) {
    run_console_child(L"RawTty.globalTerminalOperationsDoNotNeedAHandle");
    return;
  }
#endif
  const auto reset = uv::raw::tty_reset_mode();
  EXPECT_TRUE(reset);
#if UVPP_HAS_TTY_VTERM_STATE
  const auto prior = uv::raw::tty_get_vterm_state();
  uv::raw::tty_set_vterm_state(uv::tty_vterm_state::supported);
  const auto state = uv::raw::tty_get_vterm_state();
  if (state) {
    EXPECT_TRUE(state.value() == uv::tty_vterm_state::supported ||
                state.value() == uv::tty_vterm_state::unsupported);
  } else {
    EXPECT_EQ(state.error(), uv::make_error_code(UV_ENOTSUP));
  }
  if (prior) uv::raw::tty_set_vterm_state(prior.value());
#endif
}

#ifndef _WIN32
TEST(RawTty, pseudoTerminalCarriesRealReadsAndWritesAndReportsSize) {
  pty_pair terminals;
  ASSERT_TRUE(terminals.open());
  winsize requested{};
  requested.ws_col = 80;
  requested.ws_row = 24;
  ASSERT_EQ(ioctl(terminals.slave, TIOCSWINSZ, &requested), 0);
  termios initial{};
  ASSERT_EQ(tcgetattr(terminals.slave, &initial), 0);
  initial.c_lflag |= ICANON | ECHO;
  ASSERT_EQ(tcsetattr(terminals.slave, TCSANOW, &initial), 0);
  uv::loop loop;
  tty handle{loop, terminals.slave, true};
  terminals.after_init(handle);
  const int terminal_fd = handle.fileno().value();
  termios before{};
  ASSERT_EQ(tcgetattr(terminal_fd, &before), 0);
  ASSERT_EQ(before.c_lflag & (ICANON | ECHO), static_cast<tcflag_t>(ICANON | ECHO));
  ASSERT_TRUE(handle.set_mode(uv::tty_mode::raw));
  termios raw{};
  ASSERT_EQ(tcgetattr(terminal_fd, &raw), 0);
  EXPECT_EQ(raw.c_lflag & ICANON, 0u);
  EXPECT_EQ(raw.c_lflag & ECHO, 0u);
#if UVPP_HAS_TTY_RAW_VT
  ASSERT_TRUE(handle.set_mode(uv::tty_mode::raw_vt));
#endif
  const auto size = handle.size();
  ASSERT_TRUE(size);
  EXPECT_EQ(size.value().width, 80);
  EXPECT_EQ(size.value().height, 24);
  auto owner = std::make_shared<std::array<char, 128>>();
  std::weak_ptr<std::array<char, 128>> weak = owner;
  std::string received;
  ASSERT_TRUE(handle.read_start([owner](tty&, std::size_t) { return uv::buffer_view{owner->data(), owner->size()}; },
    [&](tty &self, uv::read_result result) {
      ASSERT_TRUE(result);
      if (result.count() > 0) {
        received.append(result.storage().data(), static_cast<std::size_t>(result.count()));
        EXPECT_TRUE(self.read_stop());
        EXPECT_FALSE(weak.expired());
      }
    }));
  owner.reset();
  EXPECT_EQ(handle.set_mode(static_cast<uv::tty_mode>(-1)).error(), uv::make_error_code(UV_EINVAL));
  ASSERT_EQ(::write(terminals.master, "ping", 4), 4);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (received.empty() && std::chrono::steady_clock::now() < deadline) {
    loop.run(uv::run_mode::nowait);
    std::this_thread::yield();
  }
  EXPECT_EQ(received, "ping");
  EXPECT_TRUE(weak.expired());
  uv::raw::write_request request;
  bool completed = false;
  char reply[] = "pong";
  ASSERT_TRUE(handle.write(request, uv::buffer_view{reply, 4}, [&](uv::raw::write_request &self, uv::status result) {
    EXPECT_FALSE(self.pending()); EXPECT_TRUE(result); completed = true;
  }));
  while (!completed && std::chrono::steady_clock::now() < deadline) {
    loop.run(uv::run_mode::nowait);
    std::this_thread::yield();
  }
  EXPECT_TRUE(completed);
  std::array<char, 16> output{};
  const int flags = fcntl(terminals.master, F_GETFL);
  ASSERT_NE(flags, -1);
  ASSERT_EQ(fcntl(terminals.master, F_SETFL, flags | O_NONBLOCK), 0);
  ssize_t nread = -1;
  while (nread < 0 && std::chrono::steady_clock::now() < deadline) {
    nread = ::read(terminals.master, output.data(), output.size());
    if (nread < 0 && errno != EAGAIN && errno != EINTR) break;
    std::this_thread::yield();
  }
  EXPECT_EQ(nread, 4);
  if (nread == 4) { EXPECT_EQ(std::string(output.data(), 4), "pong"); }
  EXPECT_TRUE(handle.set_mode(uv::tty_mode::normal));
  termios restored{};
  ASSERT_EQ(tcgetattr(terminal_fd, &restored), 0);
  EXPECT_EQ(restored.c_lflag & (ICANON | ECHO), before.c_lflag & (ICANON | ECHO));
  handle.close(); loop.run();
  EXPECT_FALSE(request.pending());
  EXPECT_NO_THROW(loop.close());
}
TEST(RawTty, sizeRetainsNativeErrorForNonTerminalStream) {
  int descriptors[2]{};
  ASSERT_EQ(::pipe(descriptors), 0);
  uv::loop loop;
  tty handle;
  const auto initialized = handle.init(loop, descriptors[1], false);
  if (!initialized) { ::close(descriptors[0]); ::close(descriptors[1]); }
  ASSERT_TRUE(initialized);
  int width = 0, height = 0;
  const int native_error = uv_tty_get_winsize(handle.native(), &width, &height);
  EXPECT_LT(native_error, 0);
  EXPECT_EQ(handle.size().error(), uv::make_error_code(native_error));
  handle.close(); loop.run();
  EXPECT_NO_THROW(loop.close());
  ::close(descriptors[0]);
}

TEST(RawTty, closeFromRealReadRetainsAllocatorStorageUntilDeliveryReturns) {
  pty_pair terminals;
  ASSERT_TRUE(terminals.open());
  uv::loop loop;
  tty handle{loop, terminals.slave, true};
  terminals.after_init(handle);
  ASSERT_TRUE(handle.set_mode(uv::tty_mode::raw));
  auto owner = std::make_shared<std::array<char, 32>>();
  std::weak_ptr<std::array<char, 32>> weak = owner;
  bool closed = false;
  ASSERT_TRUE(handle.read_start([owner](tty&, std::size_t) { return uv::buffer_view{owner->data(), owner->size()}; },
    [&](tty &self, uv::read_result result) {
      ASSERT_TRUE(result);
      if (result.count() == 0) return;
      EXPECT_TRUE(self.set_mode(uv::tty_mode::normal));
      self.close([&](tty&) { EXPECT_TRUE(weak.expired()); closed = true; });
      ASSERT_FALSE(weak.expired());
      EXPECT_EQ(result.storage()[0], 'x');
    }));
  owner.reset();
  ASSERT_EQ(::write(terminals.master, "x", 1), 1);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!closed && std::chrono::steady_clock::now() < deadline) {
    loop.run(uv::run_mode::nowait);
    std::this_thread::yield();
  }
  if (!closed) { handle.close(); loop.run(); }
  EXPECT_TRUE(closed);
  EXPECT_TRUE(weak.expired());
  EXPECT_NO_THROW(loop.close());
}
#else
TEST(RawTty, nativeNonConsoleFailureLeavesNoRegisteredHandleAndCanRetry) {
#ifdef _WIN32
  if (!is_console_child()) {
    run_console_child(L"RawTty.nativeNonConsoleFailureLeavesNoRegisteredHandleAndCanRetry");
    return;
  }
#endif
  uv::loop loop;
  tty handle;
  int marker = 7;
  handle.user_data(marker);
  std::vector<int> descriptors;
  int fd = _open("NUL", _O_RDWR | _O_BINARY);
  ASSERT_GE(fd, 0);
  descriptors.push_back(fd);
  // Exercise console interrogation after a valid OS handle has been obtained,
  // without the special native duplication path for standard descriptors.
  while (fd <= 2) {
    fd = _dup(fd);
    ASSERT_GE(fd, 0);
    descriptors.push_back(fd);
  }
  const auto failed = handle.init(loop, fd, false);
  EXPECT_FALSE(failed);
  EXPECT_FALSE(handle.initialized());
  EXPECT_EQ(handle.user_data<int>(), &marker);
  EXPECT_TRUE(loop.handles().empty());
  for (const int descriptor : descriptors) _close(descriptor);
  fd = open_console("CONOUT$");
  ASSERT_GE(fd, 0);
  const auto retried = handle.init(loop, fd, false);
  if (!retried) _close(fd);
  ASSERT_TRUE(retried);
  handle.close(); loop.run();
  if (fd <= 2) _close(fd);
  EXPECT_NO_THROW(loop.close());
}

TEST(RawTty, nativeConsoleInputAndOutputUseRealHandles) {
#ifdef _WIN32
  if (!is_console_child()) {
    run_console_child(L"RawTty.nativeConsoleInputAndOutputUseRealHandles");
    return;
  }
#endif
  int input_fd = open_console("CONIN$");
  int output_fd = open_console("CONOUT$");
  ASSERT_GE(input_fd, 0);
  ASSERT_GE(output_fd, 0);
  uv::loop loop;
  tty input, output;
  auto input_result = input.init(loop, input_fd, true);
  if (!input_result) _close(input_fd);
  ASSERT_TRUE(input_result);
  auto output_result = output.init(loop, output_fd, false);
  if (!output_result) _close(output_fd);
  ASSERT_TRUE(output_result);
  EXPECT_TRUE(input.readable());
  EXPECT_TRUE(output.writable());
  EXPECT_EQ(output.set_mode(uv::tty_mode::raw).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(output.size());
  std::array<char, 16> storage{};
  std::string received;
  EXPECT_TRUE(input.read_start([&](tty&, std::size_t) { return uv::buffer_view{storage.data(), storage.size()}; },
    [&](tty&, uv::read_result result) {
      EXPECT_TRUE(result);
      if (result.count() > 0) received.append(result.storage().data(), static_cast<std::size_t>(result.count()));
    }));
  EXPECT_EQ(input.set_mode(static_cast<uv::tty_mode>(-1)).error(), uv::make_error_code(UV_EINVAL));
  EXPECT_TRUE(input.set_mode(uv::tty_mode::raw));
  // Use the same real console-input mechanism as libuv's Windows tty tests.
  INPUT_RECORD key{};
  key.EventType = KEY_EVENT;
  key.Event.KeyEvent.bKeyDown = TRUE;
  key.Event.KeyEvent.wRepeatCount = 1;
  key.Event.KeyEvent.wVirtualKeyCode = 'U';
  key.Event.KeyEvent.uChar.UnicodeChar = L'u';
  DWORD written = 0;
  const auto native_input = reinterpret_cast<HANDLE>(_get_osfhandle(input_fd));
  EXPECT_TRUE(WriteConsoleInputW(native_input, &key, 1, &written));
  EXPECT_EQ(written, 1u);
  const auto read_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (received.find('u') == std::string::npos && std::chrono::steady_clock::now() < read_deadline) {
    loop.run(uv::run_mode::nowait);
    std::this_thread::yield();
  }
  EXPECT_NE(received.find('u'), std::string::npos);
  EXPECT_TRUE(input.set_mode(uv::tty_mode::normal));
  EXPECT_TRUE(input.read_stop());
  uv::raw::write_request request;
  char text[] = "uvpp tty test";
  bool completed = false;
  ASSERT_TRUE(output.write(request, uv::buffer_view{text, sizeof(text) - 1},
    [&](uv::raw::write_request &self, uv::status result) { EXPECT_FALSE(self.pending()); EXPECT_TRUE(result); completed = true; }));
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!completed && std::chrono::steady_clock::now() < deadline) {
    loop.run(uv::run_mode::nowait);
    std::this_thread::yield();
  }
  EXPECT_TRUE(completed);
  input.close(); output.close(); loop.run();
  EXPECT_NO_THROW(loop.close());
  if (input_fd <= 2) _close(input_fd);
  if (output_fd <= 2) _close(output_fd);
}
#endif

#ifdef __APPLE__
TEST(RawTtyDeathTest, nativeLateInitFailureUnlinksRegistrationAndAllowsRetry) {
  EXPECT_EXIT({
    uv::loop loop;
    pty_pair terminals;
    if (!terminals.open()) std::_Exit(10);
    rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) std::_Exit(11);
    if (limit.rlim_cur > 128) limit.rlim_cur = 128;
    if (setrlimit(RLIMIT_NOFILE, &limit) != 0) std::_Exit(12);
    std::vector<int> descriptors;
    descriptors.reserve(128);
    int fd;
    while ((fd = ::open("/dev/null", O_RDONLY)) >= 0) descriptors.push_back(fd);
    if (errno != EMFILE) std::_Exit(13);
    tty handle;
    int marker = 7;
    handle.user_data(marker);
    const auto rejected = handle.init(loop, terminals.slave, true);
    if (rejected || rejected.error() != uv::make_error_code(UV_EMFILE)) std::_Exit(14);
    if (handle.initialized() || handle.user_data<int>() != &marker || !loop.handles().empty()) std::_Exit(15);
    for (int descriptor : descriptors) ::close(descriptor);
    if (!handle.init(loop, terminals.slave, true)) std::_Exit(16);
    terminals.after_init(handle);
    handle.close(); loop.run();
    try { loop.close(); } catch (...) { std::_Exit(17); }
    std::_Exit(0);
  }, ::testing::ExitedWithCode(0), "");
}
#endif
}
