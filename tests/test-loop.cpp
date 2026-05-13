#include <algorithm>
#include <chrono>
#include <ranges>
#include <vector>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

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
  EXPECT_EQ(loop.backend_timeout(), 0);

  uv::timer timer(loop);
  timer.start(std::chrono::seconds{10}, [](uv::timer &) {});

  // With a pending timer the loop must wait; timeout is >= 0 and not -1
  // (which would mean "no timeout", i.e. block indefinitely).
  EXPECT_GE(loop.backend_timeout(), 0);
  EXPECT_NE(loop.backend_timeout(), -1);

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

  uint64_t idle_before = loop.metrics_idle_time();

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
  loop.run(UV_RUN_NOWAIT);

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
    if (h.type() == UV_TIMER) ++timer_count;
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
                    return h.type() == UV_TIMER;
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

  std::vector<uv_handle_type> types;
  loop.walk([&](uv::handle_view h) {
    types.push_back(h.type());
  });

  EXPECT_EQ(types.size(), 2u);
  EXPECT_TRUE(std::all_of(types.begin(), types.end(),
                           [](auto t) { return t == UV_TIMER; }));

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
    if (h.type() == UV_TIMER) {
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

// ---------------------------------------------------------------------------
// handle_view: implicit conversion from basic_handle
// ---------------------------------------------------------------------------

TEST(Uvpp2HandleView, basicHandleIsImplicitlyConvertibleToHandleView) {
  uv::loop loop;
  uv::timer timer(loop);

  uv::handle_view view = timer;
  EXPECT_EQ(view.type(), UV_TIMER);
  EXPECT_EQ(view.native_handle(), timer.native_handle());
  EXPECT_FALSE(view.closing());

  timer.close();
  loop.run();
  loop.close();
}

TEST(Uvpp2HandleView, handleViewRefAndUnrefAdjustReferenceCount) {
  uv::loop loop;
  uv::timer timer(loop);

  uv::handle_view view = timer;
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
