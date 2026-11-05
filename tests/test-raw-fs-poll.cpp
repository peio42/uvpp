#include <array>
#include <atomic>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>

#include "gtest/gtest.h"
#include "platform-support.hpp"
#include "uvpp/handles/fs_poll.hpp"

using namespace std::chrono_literals;
namespace {
using watcher = uv::raw::fs_poll;
using event = uv::raw::fs_poll_result;

struct RawFsPoll : ::testing::Test {
  uv::loop loop;
  watcher poll;
  std::filesystem::path dir = uvpp::test::filesystem_test_directory() /
    ("uvpp-raw-fs-poll-" + std::to_string(uv_os_getpid()) + "-" +
     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  RawFsPoll() { std::filesystem::create_directory(dir); }
  ~RawFsPoll() override {
    if (poll.initialized() && !poll.closing()) poll.close();
    loop.run();
    EXPECT_NO_THROW(loop.close());
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
  }
  template<class Predicate> void until(Predicate done) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!done() && std::chrono::steady_clock::now() < deadline) {
      loop.run(uv::run_mode::nowait);
      std::this_thread::sleep_for(1ms);
    }
    EXPECT_TRUE(done());
    if (!done()) { poll.close(); loop.run(); }
  }
};
bool write_text(const std::string &path, const char *text) {
  std::FILE *file = nullptr;
#ifdef _MSC_VER
  if (fopen_s(&file, path.c_str(), "wb") != 0) return false;
#else
  file = std::fopen(path.c_str(), "wb");
  if (!file) return false;
#endif
  const auto size = std::strlen(text);
  const bool written = std::fwrite(text, 1, size, file) == size;
  return std::fclose(file) == 0 && written;
}
void unused(watcher&, event) { ADD_FAILURE(); }
void close_counter(watcher &self) { ++*self.user_data<int>(); }

// Used only in the re-executed death-test process, before any libuv operation.
std::atomic<bool> fail_native_calloc{false};
void *native_malloc(std::size_t size) { return std::malloc(size); }
void *native_realloc(void *ptr, std::size_t size) { return std::realloc(ptr, size); }
void *native_calloc(std::size_t count, std::size_t size) {
  if (fail_native_calloc.load()) return nullptr;
  return std::calloc(count, size);
}
void native_free(void *ptr) { std::free(ptr); }

TEST(RawFsPollDeathTest, nativeAllocationFailureReleasesCallbackAndAllowsRetry) {
  EXPECT_EXIT({
    if (uv_replace_allocator(native_malloc, native_realloc, native_calloc, native_free) != 0) std::_Exit(1);
    uv::loop loop;
    watcher poll;
    int marker = 5;
    poll.user_data(marker);
    if (!poll.init(loop)) std::_Exit(2);
    auto token = std::make_shared<int>(1);
    std::weak_ptr<int> weak = token;
    fail_native_calloc = true;
    const auto failure = poll.start("uvpp-nonexistent-file", 5ms, [token](watcher&, event) {});
    fail_native_calloc = false;
    token.reset();
    if (failure.error() != uv::make_error_code(UV_ENOMEM) || !weak.expired() ||
        poll.active() || poll.user_data<int>() != &marker) std::_Exit(3);
    if (!poll.start("uvpp-nonexistent-file", 5ms, [](watcher&, event) {})) std::_Exit(4);
    if (!poll.stop()) std::_Exit(5);
    poll.close(); loop.run();
    try { loop.close(); } catch (...) { std::_Exit(6); }
    std::_Exit(0);
  }, ::testing::ExitedWithCode(0), "");
}

TEST(RawFsPollNative, startingAnActiveHandleDoesNotReplaceItsPath) {
  uv::loop loop;
  uv_fs_poll_t native{};
  ASSERT_EQ(uv_fs_poll_init(loop.native(), &native), 0);
  const auto callback = [](uv_fs_poll_t*, int, const uv_stat_t*, const uv_stat_t*) {};
  ASSERT_EQ(uv_fs_poll_start(&native, callback, "uvpp-first-missing", 10), 0);
  ASSERT_EQ(uv_fs_poll_start(&native, callback, "uvpp-second-missing", 20), 0);
  std::array<char, 128> buffer{};
  std::size_t length = buffer.size();
  ASSERT_EQ(uv_fs_poll_getpath(&native, buffer.data(), &length), 0);
  EXPECT_EQ(std::string(buffer.data(), length), "uvpp-first-missing");
  uv_close(reinterpret_cast<uv_handle_t *>(&native), nullptr);
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST_F(RawFsPoll, initializationGuardsAndNativeIdentity) {
  static_assert(std::is_nothrow_default_constructible_v<watcher>);
  static_assert(!std::is_copy_constructible_v<watcher> && !std::is_move_constructible_v<watcher>);
  static_assert(!std::is_convertible_v<watcher&, uv_fs_poll_t*>);
  static_assert(noexcept(poll.init(loop)) && noexcept(poll.stop()));
  static_assert(std::is_same_v<decltype(poll.path()), uv::result<std::string>>);
  int marker = 17;
  poll.user_data(marker);
  auto *native = poll.native();
  EXPECT_FALSE(poll.initialized());
  EXPECT_FALSE(poll.active()); EXPECT_FALSE(poll.closing()); EXPECT_FALSE(poll.has_ref());
  EXPECT_EQ(poll.start("x", 1ms, unused).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(poll.start_static<unused>("x", 1ms).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(poll.stop().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(poll.path().error(), uv::make_error_code(UV_EBADF));
  poll.close(); poll.close_static<close_counter>();
  EXPECT_TRUE(loop.handles().empty());
  ASSERT_TRUE(poll.init(loop.view()));
  EXPECT_EQ(poll.init(loop).error(), uv::make_error_code(UV_EALREADY));
  EXPECT_EQ(poll.native(), native);
  EXPECT_EQ(poll.user_data<int>(), &marker);
  EXPECT_EQ(&watcher::from_native(native), &poll);
  EXPECT_EQ(poll.path().error(), uv::make_error_code(UV_EINVAL));
  bool closed = false;
  poll.close([&](watcher &self) { EXPECT_TRUE(self.initialized()); closed = true; });
  EXPECT_EQ(poll.start("x", 1ms, unused).error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(poll.stop().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(poll.path().error(), uv::make_error_code(UV_EBADF));
  EXPECT_EQ(poll.init(loop).error(), uv::make_error_code(UV_EALREADY));
  loop.run();
  EXPECT_TRUE(closed);
}

TEST_F(RawFsPoll, resultsKeepNativeStatPointersAndNormalizedErrors) {
  uv_stat_t previous{}, current{};
  previous.st_size = 3; current.st_size = 9;
  for (const int code : {0, 7, int{UV_ENOENT}}) {
    event value{code, &previous, &current};
    EXPECT_EQ(value.previous(), &previous);
    EXPECT_EQ(value.current(), &current);
    EXPECT_EQ(value.previous_status().size(), 3u);
    EXPECT_EQ(value.current_status().size(), 9u);
    EXPECT_EQ(value.raw_status(), code < 0 ? code : 0);
    EXPECT_EQ(value.error_code(), uv::make_error_code(code));
    EXPECT_EQ(value.status().error(), value.error_code());
  }
}

TEST_F(RawFsPoll, invalidDurationsAndPathsKeepActiveSubscription) {
  ASSERT_TRUE(poll.init(loop));
  const auto target = (dir / "missing").string();
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  ASSERT_TRUE(poll.start(target, 25ms, [token](watcher&, event) {}));
  token.reset();
  const auto error = uv::make_error_code(UV_EINVAL);
  EXPECT_EQ(poll.start(target, -1ms, unused).error(), error);
  EXPECT_EQ(poll.start(target, 0ms, unused).error(), error);
  EXPECT_EQ(poll.start(target, 999us, unused).error(), error);
  EXPECT_EQ(poll.start(target, std::chrono::duration<double, std::milli>{0.5}, unused).error(), error);
  EXPECT_EQ(poll.start(target, std::chrono::duration<double>{std::numeric_limits<double>::infinity()}, unused).error(), error);
  EXPECT_EQ(poll.start(target, std::chrono::duration<double>{std::numeric_limits<double>::quiet_NaN()}, unused).error(), error);
  EXPECT_EQ(poll.start(target, std::chrono::milliseconds{4294967296LL}, unused).error(), error);
  EXPECT_EQ(poll.start_static<unused>(target, -1ms).error(), error);
  EXPECT_EQ(poll.start(std::string_view{"x\0y", 3}, 1ms, unused).error(), error);
  EXPECT_TRUE(poll.active());
  EXPECT_FALSE(weak.expired());
  EXPECT_EQ(poll.path().value(), target);
  EXPECT_EQ(poll.start(target, std::chrono::duration<double, std::milli>{1.9}, unused).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_FALSE(weak.expired());
  EXPECT_TRUE(poll.stop());
  EXPECT_TRUE(weak.expired());
  loop.run(); // Let libuv retire the stopped native poll context before reusing the handle.
  ASSERT_TRUE(poll.start(target, std::chrono::duration<double, std::milli>{1.9}, unused));
  EXPECT_TRUE(poll.stop());
  loop.run();
  ASSERT_TRUE(poll.start(target, std::chrono::milliseconds{4294967295LL}, unused));
  EXPECT_TRUE(poll.stop());
}

struct rearm_state {
  std::string first, second;
  int first_calls = 0, static_calls = 0, final_calls = 0;
  std::weak_ptr<int> old_owner;
  bool done = false;
};
void final_poll(watcher &self, event result) {
  auto &state = *self.user_data<rearm_state>();
  EXPECT_EQ(result.error_code(), uv::make_error_code(UV_ENOENT));
  EXPECT_TRUE(self.active());
  ++state.final_calls;
  EXPECT_TRUE(self.stop());
  EXPECT_EQ(self.start(state.first, 5ms, final_poll).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(self.start_static<final_poll>(state.first, 5ms).error(), uv::make_error_code(UV_EBUSY));
  state.done = true;
}
void static_poll(watcher &self, event result) {
  auto &state = *self.user_data<rearm_state>();
  ASSERT_TRUE(result);
  ASSERT_NE(result.previous(), nullptr);
  ASSERT_NE(result.current(), nullptr);
  EXPECT_TRUE(self.active());
  ++state.static_calls;
  EXPECT_EQ(self.path().value(), state.first);
  EXPECT_TRUE(self.start(state.first, 5ms, final_poll));
  EXPECT_EQ(result.current_status().size(), 7u); // Borrow remains valid during replacement.
  std::filesystem::remove(state.first);
}
TEST_F(RawFsPoll, actualNotificationsAllowReentrantCallbackReplacementAndStop) {
  ASSERT_TRUE(poll.init(loop));
  rearm_state state;
  state.first = (dir / "first").string();
  state.second = (dir / "second").string();
  poll.user_data(state);
  auto owner = std::make_shared<int>(1);
  state.old_owner = owner;
  ASSERT_TRUE(poll.start(state.first, 5ms, [&, owner](watcher &self, event result) {
    EXPECT_EQ(result.error_code(), uv::make_error_code(UV_ENOENT));
    EXPECT_TRUE(self.active()); // Poll errors are notifications, not terminal.
    ++state.first_calls;
    EXPECT_EQ(self.start_static<static_poll>(state.second, 5ms).error(), uv::make_error_code(UV_EBUSY));
    EXPECT_TRUE(self.start_static<static_poll>(state.first, 5ms));
    EXPECT_EQ(self.path().value(), state.first);
    EXPECT_FALSE(state.old_owner.expired());
    EXPECT_TRUE(write_text(state.first, "created"));
  }));
  owner.reset();
  until([&] { return state.done; });
  EXPECT_EQ(state.first_calls, 1);
  EXPECT_EQ(state.static_calls, 1);
  EXPECT_EQ(state.final_calls, 1);
  EXPECT_TRUE(state.old_owner.expired());
  EXPECT_FALSE(poll.active());
  loop.run(); // Native context cleanup completes before a new path is started.
  bool restarted = false;
  ASSERT_TRUE(poll.start(state.second, 5ms, [&](watcher &self, event result) {
    EXPECT_EQ(result.error_code(), uv::make_error_code(UV_ENOENT));
    EXPECT_TRUE(self.stop());
    restarted = true;
  }));
  until([&] { return restarted; });
}

TEST_F(RawFsPoll, activeStartPreservesNativePathAndReplacesOnlyMatchingCallback) {
  ASSERT_TRUE(poll.init(loop));
  const auto first = (dir / "old").string();
  const auto second = (dir / "new").string();
  auto token = std::make_shared<int>(1);
  std::weak_ptr<int> weak = token;
  int old_calls = 0, failures = 0, recovered = 0;
  ASSERT_TRUE(poll.start(first, 10ms, [&, token](watcher&, event) { ++old_calls; }));
  token.reset();
  EXPECT_EQ(poll.start(second, 10ms, unused).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(poll.start(first, 11ms, unused).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(poll.path().value(), first);
  EXPECT_FALSE(weak.expired());
  ASSERT_TRUE(poll.start(first, 10ms, [&](watcher &self, event result) {
    if (!result) {
      EXPECT_EQ(result.error_code(), uv::make_error_code(UV_ENOENT));
      EXPECT_TRUE(self.active());
      ++failures;
      EXPECT_TRUE(write_text(first, "created"));
      return;
    }
    ASSERT_NE(result.previous(), nullptr);
    ASSERT_NE(result.current(), nullptr);
    if (recovered == 0) {
      EXPECT_EQ(result.current_status().size(), 7u);
      ++recovered;
      EXPECT_TRUE(write_text(first, "changed-longer"));
      return;
    }
    EXPECT_EQ(result.previous_status().size(), 7u);
    EXPECT_EQ(result.current_status().size(), 14u);
    ++recovered;
    EXPECT_TRUE(self.stop());
  }));
  EXPECT_TRUE(weak.expired());
  until([&] { return recovered == 2; });
  EXPECT_EQ(old_calls, 0);
  EXPECT_EQ(failures, 1);
  EXPECT_FALSE(poll.active());
}

TEST_F(RawFsPoll, longNativePathAndStaticClose) {
  auto long_dir = dir / std::string(150, 'a');
  const auto target = (long_dir / std::string(150, 'b')).string();
  ASSERT_GT(target.size(), 256u);
  ASSERT_TRUE(poll.init(loop));
  ASSERT_TRUE(poll.start_static<unused>(target, 1s));
  EXPECT_EQ(poll.path().value(), target);
  EXPECT_TRUE(poll.start(target, 1s, unused));
  EXPECT_EQ(poll.start_static<unused>(target.substr(0, target.size() - 1), 1s).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(poll.start_static<unused>(target + "x", 1s).error(), uv::make_error_code(UV_EBUSY));
  EXPECT_EQ(poll.path().value(), target);
  int closed = 0;
  poll.user_data(closed);
  poll.close_static<close_counter>();
  loop.run();
  EXPECT_EQ(closed, 1);
}
}
