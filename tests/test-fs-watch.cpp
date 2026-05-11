#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

using namespace std::chrono_literals;

namespace {

std::filesystem::path watch_path(const char *name) {
  return std::filesystem::temp_directory_path() /
         ("uvpp-watch-" + std::to_string(::getpid()) + "-" + name);
}

void write_text(const std::filesystem::path &path, const char *text) {
  std::ofstream file{path};
  file << text;
}

struct static_poll_state {
  uvpp::timer *modifier = nullptr;
  uvpp::timer *timeout = nullptr;
  bool changed = false;
};

static_poll_state *current_static_poll = nullptr;

void on_static_fs_poll(uvpp::fs_poll &poll, uvpp::fs_poll_result result) {
  ASSERT_TRUE(result);
  ASSERT_NE(result.previous(), nullptr);
  ASSERT_NE(result.current(), nullptr);

  if (result.current()->st_size != result.previous()->st_size) {
    current_static_poll->changed = true;
    poll.stop();
    poll.close();
    current_static_poll->modifier->close();
    current_static_poll->timeout->close();
  }
}

}

TEST(Uvpp2FsEvent, reportsDirectoryChanges) {
  auto dir = watch_path("event-dir");
  auto file = dir / "created.txt";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directory(dir);

  uvpp::loop loop;
  uvpp::fs_event watcher(loop);
  uvpp::timer modifier(loop);
  uvpp::timer timeout(loop);
  bool changed = false;

  watcher.start(dir.string(), [&](uvpp::fs_event &self, uvpp::fs_event_result event) {
    ASSERT_TRUE(event);
    EXPECT_TRUE(event.has(uvpp::fs_event_kind::rename) || event.has(uvpp::fs_event_kind::change));

    changed = true;
    self.stop();
    self.close();
    modifier.close();
    timeout.close();
  });

  EXPECT_EQ(watcher.path(), dir.string());

  modifier.start(20ms, [&](uvpp::timer &timer) {
    write_text(file, "created");
    timer.stop();
  });

  timeout.start(2s, [&](uvpp::timer &timer) {
    if (!changed) {
      watcher.stop();
      watcher.close();
      modifier.close();
    }
    timer.close();
  });

  loop.run();
  EXPECT_TRUE(changed);
  loop.close();

  std::filesystem::remove_all(dir);
}

TEST(Uvpp2FsPoll, reportsFileChanges) {
  auto file = watch_path("poll.txt");
  std::filesystem::remove(file);
  write_text(file, "a");

  uvpp::loop loop;
  uvpp::fs_poll watcher(loop);
  uvpp::timer modifier(loop);
  uvpp::timer timeout(loop);
  bool changed = false;

  watcher.start(file.string(), 10ms, [&](uvpp::fs_poll &self, uvpp::fs_poll_result result) {
    ASSERT_TRUE(result);
    ASSERT_NE(result.previous(), nullptr);
    ASSERT_NE(result.current(), nullptr);

    if (result.current()->st_size != result.previous()->st_size) {
      changed = true;
      self.stop();
      self.close();
      modifier.close();
      timeout.close();
    }
  });

  EXPECT_EQ(watcher.path(), file.string());

  modifier.start(30ms, [&](uvpp::timer &timer) {
    write_text(file, "changed");
    timer.stop();
  });

  timeout.start(2s, [&](uvpp::timer &timer) {
    if (!changed) {
      watcher.stop();
      watcher.close();
      modifier.close();
    }
    timer.close();
  });

  loop.run();
  EXPECT_TRUE(changed);
  loop.close();

  std::filesystem::remove(file);
}

TEST(Uvpp2FsPoll, runsStaticCallback) {
  auto file = watch_path("static-poll.txt");
  std::filesystem::remove(file);
  write_text(file, "a");

  uvpp::loop loop;
  uvpp::fs_poll watcher(loop);
  uvpp::timer modifier(loop);
  uvpp::timer timeout(loop);
  static_poll_state state{&modifier, &timeout, false};
  current_static_poll = &state;

  watcher.start_static<on_static_fs_poll>(file.string(), 10ms);

  modifier.start(30ms, [&](uvpp::timer &timer) {
    write_text(file, "changed");
    timer.stop();
  });

  timeout.start(2s, [&](uvpp::timer &timer) {
    if (!state.changed) {
      watcher.stop();
      watcher.close();
      modifier.close();
    }
    timer.close();
  });

  loop.run();
  EXPECT_TRUE(state.changed);
  loop.close();

  current_static_poll = nullptr;
  std::filesystem::remove(file);
}
