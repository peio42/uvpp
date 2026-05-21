#include <algorithm>
#include <chrono>
#include <concepts>
#include <ranges>
#include <type_traits>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

void record_handle_type(uv::handle_view h) {
  if (h.type() == uv::handle_type::timer) {
    auto *count = static_cast<int *>(h.native_handle()->data);
    ++*count;
  }
}

struct invalid_walk_callback {
  void operator()(int) const {}
};

} // namespace

static_assert(std::invocable<decltype(record_handle_type)&, uv::handle_view>);
static_assert(!std::invocable<invalid_walk_callback&, uv::handle_view>);

// ---------------------------------------------------------------------------
// backend_fd / backend_timeout
// ---------------------------------------------------------------------------

TEST(Uvpp2Loop, backendFdIsValidFileDescriptor) {
#ifdef _WIN32
  GTEST_SKIP() << "uv_backend_fd not available on Windows";
#endif
  uv::loop loop;
  EXPECT_GE(loop.backend_fd(), 0);
  loop.close();
}

TEST(Uvpp2Loop, backendTimeoutReflectsLoopState) {
  uv::loop loop;

  // No handles: loop would exit immediately, no blocking needed.
  auto empty_timeout = loop.backend_timeout();
  ASSERT_TRUE(empty_timeout.has_value());
  EXPECT_EQ(*empty_timeout, std::chrono::milliseconds{0});

  uv::timer timer(loop);
  timer.start(std::chrono::seconds{10}, [](uv::timer &) {});

  // With a pending timer the loop must wait. std::nullopt would mean
  // "no timeout", i.e. block indefinitely.
  auto timeout = loop.backend_timeout();
  ASSERT_TRUE(timeout.has_value());
  EXPECT_GE(*timeout, std::chrono::milliseconds{0});

  timer.close();
  loop.run();
  loop.close();
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

TEST(Uvpp2Loop, metricsIdleTimeAccumulatesAfterRun) {
  uv::loop loop;
  loop.enable_metrics_idle_time();

  auto idle_before = loop.metrics_idle_time();

  uv::timer timer(loop);
  timer.start(std::chrono::milliseconds{50}, [](uv::timer &t) { t.close(); });

  loop.run();

  EXPECT_GT(loop.metrics_idle_time(), idle_before);

  loop.close();
}

TEST(Uvpp2Loop, metricsInfoCountsLoopIterations) {
  uv::loop loop;

  uv::timer timer(loop);
  timer.start(std::chrono::milliseconds{1}, [](uv::timer &t) { t.close(); });

  loop.run();

  auto info = loop.metrics_info();
  EXPECT_GE(info.loop_count, 1u);

  loop.close();
}

TEST(Uvpp2Loop, metricsInfoIsAccessibleOnLoopView) {
  uv::loop loop;
  loop.run(uv::run_mode::nowait);

  auto info = loop.view().metrics_info();
  EXPECT_GE(info.loop_count, 0u);  // just exercises the API

  loop.close();
}

// ---------------------------------------------------------------------------
// walk / handle_view
// ---------------------------------------------------------------------------

TEST(Uvpp2Loop, handlesCollectsIntoVectorForRangeBasedFor) {
  uv::loop loop;
  uv::timer t1(loop);
  uv::timer t2(loop);
  uv::idle  idle(loop);

  t1.start(std::chrono::seconds{10}, [](uv::timer &) {});
  t2.start(std::chrono::seconds{10}, [](uv::timer &) {});
  idle.start([](uv::idle &) {});

  auto all = loop.handles();
  EXPECT_EQ(all.size(), 3u);

  int timer_count = 0;
  for (auto h : all) {
    if (h.type() == uv::handle_type::timer) ++timer_count;
  }
  EXPECT_EQ(timer_count, 2);

  t1.close(); t2.close(); idle.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Loop, handlesIsComposableWithStdRanges) {
  uv::loop loop;
  uv::timer t1(loop);
  uv::timer t2(loop);
  uv::idle  idle(loop);

  t1.start(std::chrono::seconds{10}, [](uv::timer &) {});
  t2.start(std::chrono::seconds{10}, [](uv::timer &) {});
  idle.start([](uv::idle &) {});

  auto timers = loop.handles()
                | std::views::filter([](uv::handle_view h) {
                    return h.type() == uv::handle_type::timer;
                  });

  int count = 0;
  for (auto h : timers) { (void)h; ++count; }
  EXPECT_EQ(count, 2);

  t1.close(); t2.close(); idle.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Loop, walkVisitsAllActiveHandles) {
  uv::loop loop;
  uv::timer t1(loop);
  uv::timer t2(loop);

  t1.start(std::chrono::seconds{10}, [](uv::timer &) {});
  t2.start(std::chrono::seconds{10}, [](uv::timer &) {});

  std::vector<uv::handle_type> types;
  loop.walk([&](uv::handle_view h) {
    types.push_back(h.type());
  });

  EXPECT_EQ(types.size(), 2u);
  EXPECT_TRUE(std::all_of(types.begin(), types.end(),
                           [](auto t) { return t == uv::handle_type::timer; }));

  t1.close();
  t2.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Loop, walkHandleViewExposesActiveAndClosingState) {
  uv::loop loop;
  uv::timer timer(loop);
  timer.start(std::chrono::seconds{10}, [](uv::timer &) {});

  loop.walk([](uv::handle_view h) {
    EXPECT_TRUE(h.active());
    EXPECT_FALSE(h.closing());
    EXPECT_EQ(h.native_loop(), h.native_handle()->loop);
  });

  timer.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Loop, walkCanRecoverUvppHandleViaAs) {
  uv::loop loop;
  uv::timer timer(loop);
  timer.start(std::chrono::seconds{10}, [](uv::timer &) {});

  bool found = false;
  loop.walk([&](uv::handle_view h) {
    if (h.type() == uv::handle_type::timer) {
      auto &recovered = h.as<uv::timer>();
      EXPECT_EQ(recovered.native(), timer.native());
      found = true;
    }
  });
  EXPECT_TRUE(found);

  timer.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Loop, walkWorksOnLoopView) {
  uv::loop loop;
  uv::timer timer(loop);
  timer.start(std::chrono::seconds{10}, [](uv::timer &) {});

  int count = 0;
  loop.view().walk([&](uv::handle_view) { ++count; });
  EXPECT_EQ(count, 1);

  timer.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2Loop, walkAcceptsFunctionPointerCallbacks) {
  uv::loop loop;
  uv::timer timer(loop);
  timer.start(std::chrono::seconds{10}, [](uv::timer &) {});

  int count = 0;
  timer.user_data(count);

  loop.walk(record_handle_type);
  EXPECT_EQ(count, 1);

  timer.clear_user_data();
  timer.close();
  loop.run();
  loop.close();
}

// ---------------------------------------------------------------------------
// handle_view
// ---------------------------------------------------------------------------

TEST(Uvpp2HandleView, basicHandleExposesNamedHandleView) {
  static_assert(!std::is_convertible_v<uv::timer &, uv::handle_view>);

  uv::loop loop;
  uv::timer timer(loop);

  uv::handle_view view = timer.view();
  EXPECT_EQ(view.type(), uv::handle_type::timer);
  EXPECT_EQ(view.native_handle(), timer.native_handle());
  EXPECT_FALSE(view.closing());

  timer.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2HandleView, handleViewRefAndUnrefAdjustReferenceCount) {
  uv::loop loop;
  uv::timer timer(loop);

  uv::handle_view view = timer.view();
  EXPECT_TRUE(view.has_ref());

  view.unref();
  EXPECT_FALSE(view.has_ref());

  view.ref();
  EXPECT_TRUE(view.has_ref());

  timer.close();
  loop.run();
  loop.close();
}

// ---------------------------------------------------------------------------
// fork
// ---------------------------------------------------------------------------

#ifndef _WIN32
TEST(Uvpp2Loop, configureBlockSignalAcceptsSigprof) {
  uv::loop loop;

  ASSERT_NO_THROW(loop.configure_block_signal(SIGPROF));

  loop.close();
}

TEST(Uvpp2Loop, forkReinitializesLoopInChildProcess) {
  uv::loop loop;
  uv::timer timer(loop);
  timer.start(std::chrono::milliseconds{1}, [](uv::timer &t) { t.close(); });

  pid_t pid = ::fork();
  ASSERT_NE(pid, -1);

  if (pid == 0) {
    ASSERT_NO_THROW(loop.fork());
    loop.run();
    ASSERT_NO_THROW(loop.close());
    _exit(::testing::Test::HasFatalFailure() ? 1 : 0);
  }

  // Parent: clean up its own copy of the timer.
  int status = 0;
  ::waitpid(pid, &status, 0);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);

  timer.close();
  loop.run();
  loop.close();
}
#endif

// ---------------------------------------------------------------------------
// hrtime
// ---------------------------------------------------------------------------

TEST(Uvpp2Loop, hrtimeReturnsPositiveNanoseconds) {
  auto t = uv::hrtime();
  EXPECT_GT(t.count(), 0);
}

TEST(Uvpp2Loop, hrtimeIsMonotonic) {
  auto t1 = uv::hrtime();
  auto t2 = uv::hrtime();
  EXPECT_GE(t2.count(), t1.count());
}
