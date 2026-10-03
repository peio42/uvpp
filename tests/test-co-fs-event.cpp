#include "co-test-support.hpp"

#include <chrono>
#include <type_traits>

#include "uvpp/fs_event_source.hpp"
#include "uvpp/handles/fs_event.hpp"
#include "uvpp/co/sleep.hpp"
#include "uvpp/handles/timer.hpp"

using namespace std::chrono_literals;

static_assert(!std::is_convertible_v<uv::fs_event_flags, uv::fs_event_options>);
static_assert(!std::is_convertible_v<uv::fs_event_option, uv::fs_event_flags>);
static_assert(!std::is_copy_constructible_v<uv::fs_event_source>);
static_assert(!std::is_move_constructible_v<uv::raw::fs_event>);

namespace {

class UvppV3FsEventSource : public testing::Test {
protected:
  std::filesystem::path path = uvpp::test::filesystem_test_path("watch");
  uv::loop loop;
  std::optional<uv::fs_event_source> source;

  void SetUp() override {
    std::filesystem::remove_all(path);
    std::filesystem::create_directory(path);
    source.emplace(loop, path.string());
  }
  void TearDown() override {
    source.reset();
    loop.run();
    EXPECT_NO_THROW(loop.close());
    std::filesystem::remove_all(path);
  }
  void notify(const char *filename, int events = UV_CHANGE, int status = 0) {
    // Exercise the actual trampoline with deterministic backend notifications;
    // native integration is covered separately without assuming event counts.
    uv::detail::fs_event_source_state::on_event(source->native(), filename, events, status);
  }
};

TEST_F(UvppV3FsEventSource, ownsFilenameAndMergesFlagsForTheSameName) {
  char name[] = "entry";
  notify(name, UV_RENAME);
  name[0] = 'X';
  notify("entry", UV_CHANGE);
  std::optional<uv::fs_event> event;
  auto receive = [&]() -> uv::co::task<void> {
    event = co_await source->next();
    co_await source->close();
  };
  auto execution = uv::co::spawn(loop, receive());
  ASSERT_TRUE(event);
  EXPECT_EQ(event->filename, "entry");
  EXPECT_TRUE(event->events.has(uv::fs_event_flag::rename));
  EXPECT_TRUE(event->events.has(uv::fs_event_flag::change));
  loop.run();
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, ambiguityAndMissingNamesStayAbsentUntilConsumption) {
  for (const auto *middle : {"other", static_cast<const char *>(nullptr)}) {
    notify("entry", UV_RENAME);
    notify(middle);
    notify("entry");
    std::optional<uv::result<uv::fs_event>> event;
    auto receive = [&]() -> uv::co::task<void> {
      event.emplace(co_await uv::ops::next(*source));
    };
    auto execution = uv::co::spawn(loop, receive());
    ASSERT_TRUE(execution.done());
    EXPECT_NO_THROW(execution.rethrow_if_failed());
    ASSERT_TRUE(event && *event);
    EXPECT_FALSE(event->value().filename);
    EXPECT_TRUE(event->value().events.has(uv::fs_event_flag::rename));
    EXPECT_TRUE(event->value().events.has(uv::fs_event_flag::change));
  }
  notify(nullptr);
  auto receive = [&]() -> uv::co::task<void> {
    auto event = co_await source->next();
    EXPECT_FALSE(event.filename);
  };
  auto execution = uv::co::spawn(loop, receive());
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, pendingIsConsumedOnceAndTheNextBatchHasANewName) {
  notify("first");
  int count = 0;
  auto receive = [&]() -> uv::co::task<void> {
    auto first = co_await source->next();
    EXPECT_EQ(first.filename, "first");
    ++count;
    auto second = co_await source->next();
    EXPECT_EQ(second.filename, "second");
    ++count;
  };
  auto execution = uv::co::spawn(loop, receive());
  EXPECT_FALSE(execution.done());
  EXPECT_EQ(count, 1);
  notify("second");
  EXPECT_TRUE(execution.done());
  EXPECT_EQ(count, 2);
  EXPECT_NO_THROW(execution.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, cancellationReleasesOnlyTheWaiterAndAllowsReuse) {
  std::optional<uv::result<uv::fs_event>> cancelled;
  auto wait = [&]() -> uv::co::task<void> {
    cancelled.emplace(co_await uv::ops::next(*source));
  };
  auto execution = uv::co::spawn(loop, wait());
  execution.request_stop();
  ASSERT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  ASSERT_TRUE(cancelled);
  EXPECT_EQ(cancelled->error(), uv::make_error_code(UV_ECANCELED));
  EXPECT_TRUE(uv_is_active(source->native_handle()));
  notify("later");
  auto again = [&]() -> uv::co::task<void> {
    EXPECT_EQ((co_await source->next()).filename, "later");
  };
  auto second = uv::co::spawn(loop, again());
  EXPECT_TRUE(second.done());
  EXPECT_NO_THROW(second.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, priorStopPreservesPendingAndRejectsWait) {
  notify("retained");
  auto wait = [&]() -> uv::co::task<void> {
    co_await uv::co::sleep_for(1h);
  };
  // A task catches its cancelled sleep and then attempts next in that context.
  auto stopped_wait = [&]() -> uv::co::task<void> {
    try { co_await wait(); } catch (const uv::error &) {}
    auto result = co_await uv::ops::next(*source);
    EXPECT_EQ(result.error(), uv::make_error_code(UV_ECANCELED));
  };
  auto execution = uv::co::spawn(loop, stopped_wait());
  execution.request_stop();
  // Timer cancellation completes only at native close.
  loop.run(uv::run_mode::nowait);
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  auto receive = [&]() -> uv::co::task<void> {
    EXPECT_EQ((co_await source->next()).filename, "retained");
  };
  auto second = uv::co::spawn(loop, receive());
  EXPECT_TRUE(second.done());
  EXPECT_NO_THROW(second.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, concurrentWaitIsBusyAndCloseCancelsActiveWait) {
  std::optional<uv::result<uv::fs_event>> first;
  auto wait = [&]() -> uv::co::task<void> { first.emplace(co_await uv::ops::next(*source)); };
  auto close = [&]() -> uv::co::task<void> {
    EXPECT_EQ((co_await uv::ops::next(*source)).error(), uv::make_error_code(UV_EBUSY));
    EXPECT_TRUE(co_await uv::ops::close(*source));
  };
  auto a = uv::co::spawn(loop, wait());
  auto b = uv::co::spawn(loop, close());
  ASSERT_TRUE(first);
  EXPECT_EQ(first->error(), uv::make_error_code(UV_ECANCELED));
  loop.run();
  EXPECT_TRUE(a.done());
  EXPECT_TRUE(b.done());
  EXPECT_NO_THROW(a.rethrow_if_failed());
  EXPECT_NO_THROW(b.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, callbackFailuresUseBothPoliciesAndPendingErrorWins) {
  for (bool pending : {false, true}) {
    if (pending) {
      notify("old");
      notify(nullptr, 0, UV_EIO);
      notify("later");
    }
    auto explicit_wait = [&]() -> uv::co::task<void> {
      auto result = co_await uv::ops::next(*source);
      EXPECT_EQ(result.error(), uv::make_error_code(UV_EIO));
    };
    auto a = uv::co::spawn(loop, explicit_wait());
    if (!pending) { notify(nullptr, 0, UV_EIO); }
    EXPECT_TRUE(a.done());
    EXPECT_NO_THROW(a.rethrow_if_failed());

    auto throwing_wait = [&]() -> uv::co::task<void> { co_await source->next(); };
    if (pending) { notify(nullptr, 0, UV_EIO); }
    auto b = uv::co::spawn(loop, throwing_wait());
    if (!pending) { notify(nullptr, 0, UV_EIO); }
    EXPECT_TRUE(b.done());
    EXPECT_THROW(b.rethrow_if_failed(), uv::error);
  }
}

TEST_F(UvppV3FsEventSource, moveWhileWaitingAndImmediateRearmPreserveNativeIdentity) {
  auto *native = source->native();
  int count = 0;
  auto receive = [&]() -> uv::co::task<void> {
    co_await source->next();
    ++count;
    co_await source->next();
    ++count;
  };
  auto execution = uv::co::spawn(loop, receive());
  uv::fs_event_source moved{std::move(*source)};
  EXPECT_EQ(moved.native(), native);
  EXPECT_EQ(source->native(), nullptr);
  *source = std::move(moved);
  notify("first");
  EXPECT_EQ(count, 1);
  notify("second");
  EXPECT_EQ(count, 2);
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, deliveryMayDestroyOwnerAndCloseRetainsStorage) {
  auto receive = [&]() -> uv::co::task<void> {
    co_await source->next();
    source.reset();
  };
  auto execution = uv::co::spawn(loop, receive());
  notify("entry");
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
  EXPECT_EQ(loop.try_close(), uv::make_error_code(UV_EBUSY));
  loop.run();
}

TEST_F(UvppV3FsEventSource, closeJoinsAreNonCancellableAndMayReleaseOwner) {
  notify("discarded");
  auto close = [&]() -> uv::co::task<void> { co_await source->close(); };
  auto release = [&]() -> uv::co::task<void> {
    co_await source->close();
    source.reset();
  };
  auto a = uv::co::spawn(loop, close());
  auto b = uv::co::spawn(loop, release());
  a.request_stop();
  b.request_stop();
  EXPECT_FALSE(a.done());
  EXPECT_FALSE(b.done());
  notify("late"); // ignored after close.begin()
  loop.run();
  EXPECT_TRUE(a.done());
  EXPECT_TRUE(b.done());
  EXPECT_NO_THROW(a.rethrow_if_failed());
  EXPECT_NO_THROW(b.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, closeCancellingWaiterMayReleaseOwnerReentrantly) {
  auto wait = [&]() -> uv::co::task<void> {
    EXPECT_EQ((co_await uv::ops::next(*source)).error(), uv::make_error_code(UV_ECANCELED));
    source.reset();
  };
  auto close = [&]() -> uv::co::task<void> { co_await source->close(); };
  auto a = uv::co::spawn(loop, wait());
  auto b = uv::co::spawn(loop, close());
  EXPECT_FALSE(source);
  loop.run();
  EXPECT_TRUE(a.done());
  EXPECT_TRUE(b.done());
  EXPECT_NO_THROW(a.rethrow_if_failed());
  EXPECT_NO_THROW(b.rethrow_if_failed());
}

TEST_F(UvppV3FsEventSource, rejectsWrongLoopClosedAndMovedFromWaits) {
  uv::loop other;
  notify("pending");
  auto wait = [&]() -> uv::co::task<void> { co_await uv::ops::next(*source); };
  auto wrong = uv::co::spawn(other, wait());
  EXPECT_TRUE(wrong.done());
  EXPECT_THROW(wrong.rethrow_if_failed(), std::logic_error);
  auto wrong_close_task = [&]() -> uv::co::task<void> { co_await source->close(); };
  auto wrong_close = uv::co::spawn(other, wrong_close_task());
  EXPECT_THROW(wrong_close.rethrow_if_failed(), std::logic_error);
  other.close();
  source->request_close();
  auto closed_wait = [&]() -> uv::co::task<void> {
    EXPECT_EQ((co_await uv::ops::next(*source)).error(), uv::make_error_code(UV_EBADF));
    co_await source->close();
    co_await source->close();
  };
  auto closed = uv::co::spawn(loop, closed_wait());
  loop.run();
  EXPECT_TRUE(closed.done());
  EXPECT_NO_THROW(closed.rethrow_if_failed());
  uv::fs_event_source moved{std::move(*source)};
  auto missing_wait = [&]() -> uv::co::task<void> { co_await source->next(); };
  auto missing = uv::co::spawn(loop, missing_wait());
  EXPECT_THROW(missing.rethrow_if_failed(), uv::error);
  EXPECT_THROW(source->request_close(), uv::error);
}

TEST_F(UvppV3FsEventSource, nativeNotificationsRemainSubscribedAcrossWaits) {
  uv::timer timeout{loop};
  bool timed_out = false;
  timeout.start(2s, [&](uv::timer &self) {
    timed_out = true;
    source->request_close();
    self.close();
  });
  unsigned received = 0;
  auto receive = [&]() -> uv::co::task<void> {
    for (int i = 0; i != 2; ++i) {
      auto event = co_await source->next();
      EXPECT_TRUE(event.events.has(uv::fs_event_flag::rename) ||
                  event.events.has(uv::fs_event_flag::change));
      ++received;
      if (i == 0) {
        uvpp::test::write_filesystem_test_file(path / "second", "second");
      }
    }
    timeout.close();
    co_await source->close();
  };
  auto execution = uv::co::spawn(loop, receive());
  uvpp::test::write_filesystem_test_file(path / "first", "first");
  loop.run();
  EXPECT_FALSE(timed_out);
  EXPECT_EQ(received, 2u);
  EXPECT_TRUE(execution.done());
  EXPECT_NO_THROW(execution.rethrow_if_failed());
}

} // namespace

TEST(UvppV3FsEventSetup, failedStartRetainsInitializedStorageThroughClose) {
  uv::loop loop;
  auto path = uvpp::test::filesystem_test_path("missing-watch");
  std::filesystem::remove_all(path);
  EXPECT_THROW((uv::fs_event_source{loop, path.string()}), uv::error);
  EXPECT_EQ(loop.try_close(), uv::make_error_code(UV_EBUSY));
  loop.run();
  EXPECT_NO_THROW(loop.close());
}

TEST(UvppV3FsEventSetup, embeddedNulIsRejectedBeforeNativeInitialization) {
  uv::loop loop;
  EXPECT_THROW((uv::fs_event_source{loop, std::string_view{"path\0suffix", 11}}), uv::error);
  EXPECT_NO_THROW(loop.close());
}
