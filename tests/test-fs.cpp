#include <array>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

namespace {

std::filesystem::path temp_path(const char *name) {
  return std::filesystem::temp_directory_path() /
         ("uvpp-fs-" + std::to_string(::getpid()) + "-" + name);
}

struct static_fs_state {
  uv::loop *loop = nullptr;
  uv::fs::raw::request *request = nullptr;
  std::string path;
  bool opened = false;
  bool closed = false;
};

static_fs_state *current_static_fs = nullptr;

struct static_copyfile_state {
  bool copied = false;
};

static_copyfile_state *current_static_copyfile = nullptr;

struct static_status_state {
  bool done = false;
};

static_status_state *current_static_status = nullptr;

void on_static_fs_close(uv::fs::raw::request &request, uv::fs::raw::status_result result) {
  auto cleanup = request.scoped_cleanup();
  EXPECT_TRUE(result);
  current_static_fs->closed = true;
}

void on_static_fs_open(uv::fs::raw::request &request, uv::fs::raw::open_result result) {
  uv::file_descriptor file;

  {
    auto cleanup = request.scoped_cleanup();
    EXPECT_TRUE(result);
    current_static_fs->opened = true;
    file = result.file();
  }

  uv::fs::raw::close_static<on_static_fs_close>(*current_static_fs->loop, *current_static_fs->request, file);
}

void on_static_copyfile(uv::fs::raw::request &request, uv::fs::raw::status_result result) {
  auto cleanup = request.scoped_cleanup();
  EXPECT_TRUE(result);
  current_static_copyfile->copied = true;
}

void on_static_status(uv::fs::raw::request &request, uv::fs::raw::status_result result) {
  auto cleanup = request.scoped_cleanup();
  EXPECT_TRUE(result);
  current_static_status->done = true;
}

std::string read_text_file(const std::filesystem::path &path) {
  std::ifstream file{path};
  return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

}

TEST(Uvpp2Fs, opensWritesReadsClosesAndReusesRequest) {
  auto path = temp_path("read-write.txt");
  std::filesystem::remove(path);

  uv::loop loop;
  uv::fs::raw::request request;
  std::array payload{'h', 'e', 'l', 'l', 'o'};
  uv::owned_buffer input{std::as_bytes(std::span{payload})};
  uv::owned_buffer output{payload.size()};
  bool done = false;
  int callbacks = 0;

  uv::fs::raw::open(loop, request, path.string(), O_CREAT | O_TRUNC | O_RDWR, 0644,
    [&](uv::fs::raw::request &open_request, uv::fs::raw::open_result open_result) {
      ++callbacks;
      EXPECT_EQ(&open_request, &request);
      EXPECT_EQ(open_request.type(), uv::request_type::fs);
      EXPECT_EQ(open_request.operation(), uv::fs_type::open);

      uv::file_descriptor file;
      {
        auto cleanup = open_request.scoped_cleanup();
        ASSERT_TRUE(open_result);
        file = open_result.file();
      }

      uv::fs::raw::write(loop, request, file, input.bytes(), 0,
        [&, file](uv::fs::raw::request &write_request, uv::fs::raw::byte_count_result write_result) {
          ++callbacks;
          EXPECT_EQ(&write_request, &request);
          EXPECT_EQ(write_request.type(), uv::request_type::fs);
          EXPECT_EQ(write_request.operation(), uv::fs_type::write);
          {
            auto cleanup = write_request.scoped_cleanup();
            ASSERT_TRUE(write_result);
            EXPECT_EQ(write_result.count(), payload.size());
          }

          uv::fs::raw::read(loop, request, file, output.view(), 0,
            [&, file](uv::fs::raw::request &read_request, uv::fs::raw::byte_count_result read_result) {
              ++callbacks;
              EXPECT_EQ(&read_request, &request);
              EXPECT_EQ(read_request.type(), uv::request_type::fs);
              EXPECT_EQ(read_request.operation(), uv::fs_type::read);
              {
                auto cleanup = read_request.scoped_cleanup();
                ASSERT_TRUE(read_result);
                EXPECT_EQ(read_result.count(), payload.size());
                EXPECT_EQ(std::memcmp(output.view().data(), "hello", payload.size()), 0);
              }

              uv::fs::raw::close(loop, request, file,
                [&](uv::fs::raw::request &close_request, uv::fs::raw::status_result close_result) {
                  ++callbacks;
                  auto cleanup = close_request.scoped_cleanup();
                  EXPECT_EQ(&close_request, &request);
                  EXPECT_EQ(close_request.type(), uv::request_type::fs);
                  EXPECT_EQ(close_request.operation(), uv::fs_type::close);
                  EXPECT_TRUE(close_result);
                  done = true;
                });
            });
        });
    });

  loop.run();
  EXPECT_TRUE(done);
  EXPECT_EQ(callbacks, 4);
  loop.close();

  std::filesystem::remove(path);
}

TEST(Uvpp2Fs, statsFile) {
  auto path = temp_path("stat.txt");
  std::filesystem::remove(path);
  {
    std::ofstream file{path};
    file << "stat";
  }

  uv::loop loop;
  uv::fs::raw::request request;
  bool done = false;

  uv::fs::raw::stat(loop, request, path.string(), [&](uv::fs::raw::request &request, uv::fs::raw::stat_result result) {
    auto cleanup = request.scoped_cleanup();
    ASSERT_TRUE(result);
    EXPECT_EQ(result.native().st_size, 4);
    done = true;
  });

  loop.run();
  EXPECT_TRUE(done);
  loop.close();

  std::filesystem::remove(path);
}

TEST(Uvpp2Fs, reportsOpenMissingFile) {
  auto path = temp_path("missing.txt");
  std::filesystem::remove(path);

  uv::loop loop;
  uv::fs::raw::request request;
  bool done = false;

  uv::fs::raw::open(loop, request, path.string(), O_RDONLY, 0,
    [&](uv::fs::raw::request &request, uv::fs::raw::open_result result) {
      auto cleanup = request.scoped_cleanup();
      EXPECT_FALSE(result);
      EXPECT_EQ(result.error_code(), uv::make_error_code(UV_ENOENT));
      done = true;
    });

  loop.run();
  EXPECT_TRUE(done);
  loop.close();
}

TEST(Uvpp2Fs, unlinksFile) {
  auto path = temp_path("unlink.txt");
  std::filesystem::remove(path);
  {
    std::ofstream file{path};
    file << "unlink";
  }

  uv::loop loop;
  uv::fs::raw::request request;
  bool done = false;

  uv::fs::raw::unlink(loop, request, path.string(), [&](uv::fs::raw::request &request, uv::fs::raw::status_result result) {
    auto cleanup = request.scoped_cleanup();
    EXPECT_TRUE(result);
    done = true;
  });

  loop.run();
  EXPECT_TRUE(done);
  EXPECT_FALSE(std::filesystem::exists(path));
  loop.close();
}

TEST(Uvpp2Fs, runsStaticCallbacks) {
  auto path = temp_path("static.txt");
  std::filesystem::remove(path);

  uv::loop loop;
  uv::fs::raw::request request;
  static_fs_state state{&loop, &request, path.string()};
  current_static_fs = &state;

  uv::fs::raw::open_static<on_static_fs_open>(loop, request, state.path, O_CREAT | O_TRUNC | O_RDWR, 0644);

  loop.run();
  EXPECT_TRUE(state.opened);
  EXPECT_TRUE(state.closed);
  loop.close();

  current_static_fs = nullptr;
  std::filesystem::remove(path);
}

TEST(Uvpp2Fs, resolvesRealpathAndReadlinkWithRequestScopedPaths) {
  auto target = temp_path("target.txt");
  auto link = temp_path("link.txt");
  std::filesystem::remove(link);
  std::filesystem::remove(target);
  {
    std::ofstream file{target};
    file << "target";
  }

  std::error_code symlink_error;
  std::filesystem::create_symlink(target, link, symlink_error);
  if (symlink_error) {
    std::filesystem::remove(target);
    GTEST_SKIP() << "symlink creation failed: " << symlink_error.message();
  }

  uv::loop loop;
  uv::fs::raw::request request;
  std::string readlink_path;
  std::string real_path;

  uv::fs::raw::readlink(loop, request, link.string(),
    [&](uv::fs::raw::request &request, uv::fs::raw::path_result result) {
    auto cleanup = request.scoped_cleanup();
    ASSERT_TRUE(result);
    readlink_path = result.path();
  });

  loop.run();
  EXPECT_EQ(readlink_path, target.string());

  uv::fs::raw::realpath(loop, request, target.string(),
    [&](uv::fs::raw::request &request, uv::fs::raw::path_result result) {
    auto cleanup = request.scoped_cleanup();
    ASSERT_TRUE(result);
    real_path = result.path();
  });

  loop.run();
  EXPECT_EQ(real_path, std::filesystem::canonical(target).string());
  loop.close();

  std::filesystem::remove(link);
  std::filesystem::remove(target);
}

TEST(Uvpp2Fs, copiesFilesAndSendsFileRanges) {
  auto source = temp_path("copy-source.txt");
  auto copied = temp_path("copy-destination.txt");
  auto sent = temp_path("sendfile-destination.txt");
  std::filesystem::remove(source);
  std::filesystem::remove(copied);
  std::filesystem::remove(sent);
  {
    std::ofstream file{source};
    file << "copy-send";
  }

  uv::loop loop;
  uv::fs::raw::request request;
  bool copied_done = false;
  bool sent_done = false;

  uv::fs::raw::copyfile(loop, request, source.string(), copied.string(), 0,
    [&](uv::fs::raw::request &request, uv::fs::raw::status_result result) {
      auto cleanup = request.scoped_cleanup();
      EXPECT_TRUE(result);
      copied_done = true;
    });

  loop.run();
  ASSERT_TRUE(copied_done);
  EXPECT_EQ(read_text_file(copied), "copy-send");

  int in_fd = ::open(source.c_str(), O_RDONLY);
  ASSERT_GE(in_fd, 0);
  int out_fd = ::open(sent.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
  ASSERT_GE(out_fd, 0);

  uv::fs::raw::sendfile(loop, request, uv::file_descriptor{out_fd}, uv::file_descriptor{in_fd}, 5, 4,
    [&](uv::fs::raw::request &request, uv::fs::raw::byte_count_result result) {
      auto cleanup = request.scoped_cleanup();
      ASSERT_TRUE(result);
      EXPECT_EQ(result.count(), 4u);
      sent_done = true;
    });

  loop.run();
  EXPECT_TRUE(sent_done);
  loop.close();
  ::close(in_fd);
  ::close(out_fd);

  EXPECT_EQ(read_text_file(sent), "send");

  std::filesystem::remove(source);
  std::filesystem::remove(copied);
  std::filesystem::remove(sent);
}

TEST(Uvpp2Fs, scansDirectoryEntriesWithoutCopyingInTheWrapper) {
  auto dir = temp_path("scandir");
  std::filesystem::remove_all(dir);
  std::filesystem::create_directory(dir);
  {
    std::ofstream file{dir / "alpha.txt"};
    file << "alpha";
  }
  {
    std::ofstream file{dir / "beta.txt"};
    file << "beta";
  }

  uv::loop loop;
  uv::fs::raw::request request;
  std::vector<std::string> names;

  uv::fs::raw::scandir(loop, request, dir.string(), 0,
    [&](uv::fs::raw::request &request, uv::fs::raw::scandir_result result) {
      auto cleanup = request.scoped_cleanup();
      ASSERT_TRUE(result);

      for (auto entry : result) {
        names.emplace_back(entry.name());
      }
    });

  loop.run();
  loop.close();

  EXPECT_NE(std::find(names.begin(), names.end(), "alpha.txt"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "beta.txt"), names.end());

  std::filesystem::remove_all(dir);
}

TEST(Uvpp2Fs, scandirResultIsUsableAsRangeBasedFor) {
  auto dir = temp_path("scandir-range");
  std::filesystem::remove_all(dir);
  std::filesystem::create_directory(dir);
  {
    std::ofstream file{dir / "alpha.txt"};
    file << "alpha";
  }
  {
    std::ofstream file{dir / "beta.txt"};
    file << "beta";
  }

  uv::loop loop;
  std::vector<std::string> names;

  uv::fs::scandir(loop, dir.string(), 0, [&](uv::fs::scandir_result result) {
    ASSERT_TRUE(result);
    for (const auto &entry : result) {
      names.emplace_back(entry.name);
    }
  });

  loop.run();
  loop.close();

  EXPECT_NE(std::find(names.begin(), names.end(), "alpha.txt"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "beta.txt"), names.end());

  std::filesystem::remove_all(dir);
}

TEST(Uvpp2Fs, opensReadsAndClosesDirectory) {
  auto path = temp_path("opendir");
  std::filesystem::remove_all(path);
  std::filesystem::create_directory(path);
  {
    std::ofstream file{path / "first.txt"};
    file << "first";
  }
  {
    std::ofstream file{path / "second.txt"};
    file << "second";
  }

  uv::loop loop;
  uv::fs::raw::request request;
  uv::fs::raw::directory_read_buffer buffer{8};
  uv::fs::raw::directory dir;
  std::vector<std::string> names;
  bool closed = false;

  uv::fs::raw::opendir(loop, request, path.string(),
    [&](uv::fs::raw::request &request, uv::fs::raw::opendir_result result) {
      {
        auto cleanup = request.scoped_cleanup();
        ASSERT_TRUE(result);
        dir = result.take_directory();
      }

      uv::fs::raw::readdir(loop, request, dir, buffer,
        [&](uv::fs::raw::request &request, uv::fs::raw::readdir_result result) {
          {
            auto cleanup = request.scoped_cleanup();
            ASSERT_TRUE(result);

            for (auto entry : result.entries(buffer)) {
              names.emplace_back(entry.name());
            }
          }

          uv::fs::raw::closedir(loop, request, std::move(dir),
            [&](uv::fs::raw::request &request, uv::fs::raw::status_result result) {
              auto cleanup = request.scoped_cleanup();
              EXPECT_TRUE(result);
              closed = true;
            });
        });
    });

  loop.run();
  loop.close();

  EXPECT_TRUE(closed);
  EXPECT_NE(std::find(names.begin(), names.end(), "first.txt"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "second.txt"), names.end());

  std::filesystem::remove_all(path);
}

TEST(Uvpp2Fs, failedClosedirSubmissionKeepsDirectoryOwnership) {
  uv::fs::raw::directory dir{reinterpret_cast<uv_dir_t *>(0x1)};

  EXPECT_THROW(uv::fs::raw::detail::submit_closedir(dir, [](uv_dir_t *) {
    throw uv::error{UV_EINVAL};
  }), uv::error);

  EXPECT_TRUE(dir);
  static_cast<void>(dir.release());
}

TEST(Uvpp2Fs, runsStaticCopyfileCallback) {
  auto source = temp_path("static-copy-source.txt");
  auto copied = temp_path("static-copy-destination.txt");
  std::filesystem::remove(source);
  std::filesystem::remove(copied);
  {
    std::ofstream file{source};
    file << "static-copy";
  }

  uv::loop loop;
  uv::fs::raw::request request;
  static_copyfile_state state;
  current_static_copyfile = &state;

  uv::fs::raw::copyfile_static<on_static_copyfile>(loop, request, source.string(), copied.string());

  loop.run();
  EXPECT_TRUE(state.copied);
  EXPECT_EQ(read_text_file(copied), "static-copy");
  loop.close();

  current_static_copyfile = nullptr;
  std::filesystem::remove(source);
  std::filesystem::remove(copied);
}

TEST(Uvpp2Fs, runsStaticMkdirCallback) {
  auto path = temp_path("static-mkdir");
  std::filesystem::remove_all(path);

  uv::loop loop;
  uv::fs::raw::request request;
  static_status_state state;
  current_static_status = &state;

  uv::fs::raw::mkdir_static<on_static_status>(loop, request, path.string(), 0755);

  loop.run();
  EXPECT_TRUE(state.done);
  EXPECT_TRUE(std::filesystem::is_directory(path));
  loop.close();

  current_static_status = nullptr;
  std::filesystem::remove_all(path);
}

TEST(Uvpp2FsSafe, ownsRequestsCleanupAndBuffers) {
  auto path = temp_path("safe-read-write.txt");
  std::filesystem::remove(path);

  uv::loop loop;
  std::array payload{'s', 'a', 'f', 'e'};
  bool done = false;
  int callbacks = 0;

  uv::fs::open(loop, path.string(), O_CREAT | O_TRUNC | O_RDWR, 0644,
    [&](uv::fs::open_result open_result) {
      ++callbacks;
      ASSERT_TRUE(open_result);
      auto file = open_result.file();

      uv::fs::write(loop, file, std::as_bytes(std::span{payload}), 0,
        [&, file](uv::fs::byte_count_result write_result) {
          ++callbacks;
          ASSERT_TRUE(write_result);
          EXPECT_EQ(write_result.count(), payload.size());

          uv::fs::read(loop, file, payload.size(), 0,
            [&, file](uv::fs::read_result read_result) {
              ++callbacks;
              ASSERT_TRUE(read_result);
              ASSERT_EQ(read_result.count(), payload.size());
              EXPECT_EQ(std::memcmp(read_result.bytes().data(), payload.data(), payload.size()), 0);

              uv::fs::close(loop, file, [&](uv::fs::status_result close_result) {
                ++callbacks;
                EXPECT_TRUE(close_result);
                done = true;
              });
            });
        });
    });

  loop.run();
  EXPECT_TRUE(done);
  EXPECT_EQ(callbacks, 4);
  loop.close();

  std::filesystem::remove(path);
}

TEST(Uvpp2FsSafe, returnsOwnedPathAndScandirResults) {
  auto dir = temp_path("safe-scandir");
  auto target = dir / "target.txt";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directory(dir);
  {
    std::ofstream file{target};
    file << "target";
  }

  uv::loop loop;
  std::string canonical;
  std::vector<uv::fs::directory_entry> entries;

  uv::fs::realpath(loop, target.string(), [&](uv::fs::path_result result) {
    ASSERT_TRUE(result);
    canonical = result.path();
  });

  loop.run();
  EXPECT_EQ(canonical, std::filesystem::canonical(target).string());

  uv::fs::scandir(loop, dir.string(), 0, [&](uv::fs::scandir_result result) {
    ASSERT_TRUE(result);
    entries = result.take_entries();
  });

  loop.run();
  loop.close();

  auto found = std::find_if(entries.begin(), entries.end(), [](const uv::fs::directory_entry &entry) {
    return entry.name == "target.txt";
  });
  EXPECT_NE(found, entries.end());

  std::filesystem::remove_all(dir);
}

TEST(Uvpp2FsSafe, createsRenamesAccessesAndRemovesDirectories) {
  auto path = temp_path("safe-dir");
  auto renamed = temp_path("safe-dir-renamed");
  std::filesystem::remove_all(path);
  std::filesystem::remove_all(renamed);

  uv::loop loop;
  bool mkdir_done = false;
  bool access_done = false;
  bool rename_done = false;
  bool rmdir_done = false;

  uv::fs::mkdir(loop, path.string(), 0755, [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    mkdir_done = true;
  });
  loop.run();

  uv::fs::access(loop, path.string(), F_OK, [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    access_done = true;
  });
  loop.run();

  uv::fs::rename(loop, path.string(), renamed.string(), [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    rename_done = true;
  });
  loop.run();

  uv::fs::rmdir(loop, renamed.string(), [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    rmdir_done = true;
  });
  loop.run();
  loop.close();

  EXPECT_TRUE(mkdir_done);
  EXPECT_TRUE(access_done);
  EXPECT_TRUE(rename_done);
  EXPECT_TRUE(rmdir_done);
  EXPECT_FALSE(std::filesystem::exists(path));
  EXPECT_FALSE(std::filesystem::exists(renamed));
}

TEST(Uvpp2FsSafe, handlesDescriptorOperations) {
  auto path = temp_path("safe-fd.txt");
  std::filesystem::remove(path);
  {
    std::ofstream file{path};
    file << "abcdef";
  }

  int fd = ::open(path.c_str(), O_RDWR);
  ASSERT_GE(fd, 0);
  uv::file_descriptor file{fd};

  uv::loop loop;
  bool fstat_done = false;
  bool truncate_done = false;
  bool fsync_done = false;
  bool fdatasync_done = false;
  bool second_fstat_done = false;

  uv::fs::fstat(loop, file, [&](uv::fs::stat_result result) {
    ASSERT_TRUE(result);
    EXPECT_EQ(result.native().st_size, 6);
    fstat_done = true;
  });
  loop.run();

  uv::fs::ftruncate(loop, file, 3, [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    truncate_done = true;
  });
  loop.run();

  uv::fs::fsync(loop, file, [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    fsync_done = true;
  });
  loop.run();

  uv::fs::fdatasync(loop, file, [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    fdatasync_done = true;
  });
  loop.run();

  uv::fs::fstat(loop, file, [&](uv::fs::stat_result result) {
    ASSERT_TRUE(result);
    EXPECT_EQ(result.native().st_size, 3);
    second_fstat_done = true;
  });
  loop.run();
  loop.close();

  EXPECT_TRUE(fstat_done);
  EXPECT_TRUE(truncate_done);
  EXPECT_TRUE(fsync_done);
  EXPECT_TRUE(fdatasync_done);
  EXPECT_TRUE(second_fstat_done);

  ::close(fd);
  EXPECT_EQ(read_text_file(path), "abc");
  std::filesystem::remove(path);
}

TEST(Uvpp2FsSafe, handlesLinksMetadataAndTimes) {
  auto target = temp_path("safe-link-target.txt");
  auto hardlink = temp_path("safe-hardlink.txt");
  auto symlink = temp_path("safe-symlink.txt");
  std::filesystem::remove(target);
  std::filesystem::remove(hardlink);
  std::filesystem::remove(symlink);
  {
    std::ofstream file{target};
    file << "links";
  }

  uv::loop loop;
  bool chmod_done = false;
  bool chown_done = false;
  bool utime_done = false;
  bool link_done = false;
  bool symlink_done = false;
  bool symlink_forbidden = false;
  bool lstat_done = false;

  uv::fs::chmod(loop, target.string(), 0644, [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    chmod_done = true;
  });
  loop.run();

  uv::fs::chown(loop, target.string(), static_cast<uv_uid_t>(::getuid()), static_cast<uv_gid_t>(::getgid()),
    [&](uv::fs::status_result result) {
      EXPECT_TRUE(result);
      chown_done = true;
    });
  loop.run();

  uv::fs::utime(loop, target.string(), 1000.0, 1001.0, [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    utime_done = true;
  });
  loop.run();

  uv::fs::link(loop, target.string(), hardlink.string(), [&](uv::fs::status_result result) {
    EXPECT_TRUE(result);
    link_done = true;
  });
  loop.run();

  uv::fs::symlink(loop, target.string(), symlink.string(), [&](uv::fs::status_result result) {
    if (!result && result.error_code() == uv::make_error_code(UV_EPERM)) {
      symlink_forbidden = true;
      return;
    }

    ASSERT_TRUE(result);
    symlink_done = true;
  });
  loop.run();

  if (symlink_forbidden) {
    loop.close();
    std::filesystem::remove(hardlink);
    std::filesystem::remove(target);
    GTEST_SKIP() << "symlink creation is not permitted";
  }

  uv::fs::lstat(loop, symlink.string(), [&](uv::fs::stat_result result) {
    ASSERT_TRUE(result);
    EXPECT_TRUE(S_ISLNK(result.native().st_mode));
    lstat_done = true;
  });
  loop.run();
  loop.close();

  EXPECT_TRUE(chmod_done);
  EXPECT_TRUE(chown_done);
  EXPECT_TRUE(utime_done);
  EXPECT_TRUE(link_done);
  EXPECT_TRUE(symlink_done);
  EXPECT_TRUE(lstat_done);
  EXPECT_EQ(read_text_file(hardlink), "links");

  std::filesystem::remove(symlink);
  std::filesystem::remove(hardlink);
  std::filesystem::remove(target);
}
