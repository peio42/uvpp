#include <chrono>
#include <filesystem>
#include <string>

#include "gtest/gtest.h"
#include "uvpp/uv.hpp"

TEST(Uvpp2System, environmentVariableRoundTripsAndMissingIsOptional) {
#if UVPP_HAS_OS_ENVIRONMENT
  const std::string name = "UVPP_TEST_SYSTEM_ENVIRONMENT";

  uv::unset_environment_variable(name);
  EXPECT_FALSE(uv::environment_variable(name));

  uv::set_environment_variable(name, "present");
  auto value = uv::environment_variable(name);
  ASSERT_TRUE(value);
  EXPECT_EQ(*value, "present");

  uv::unset_environment_variable(name);
  EXPECT_FALSE(uv::environment_variable(name));
#else
  GTEST_SKIP() << "uv_os_getenv is unavailable in this libuv version";
#endif
}

TEST(Uvpp2System, reportsProcessIdentityAndPriority) {
#if UVPP_HAS_OS_GETPID
  EXPECT_GT(uv::pid(), 0);
#endif

#if UVPP_HAS_OS_GETPPID
  EXPECT_GE(uv::parent_pid(), 0);
#endif

#if UVPP_HAS_OS_PRIORITY && UVPP_HAS_OS_GETPID
  (void)uv::process_priority();
#endif
}

TEST(Uvpp2System, reportsHostAndSystemInfo) {
#if UVPP_HAS_OS_GETHOSTNAME
  EXPECT_FALSE(uv::hostname().empty());
#endif

#if UVPP_HAS_OS_UNAME
  auto info = uv::uname();
  EXPECT_FALSE(info.sysname.empty());
  EXPECT_FALSE(info.release.empty());
  EXPECT_FALSE(info.machine.empty());
#endif

  auto load = uv::load_average();
  EXPECT_EQ(load.size(), 3u);

  EXPECT_GE(uv::uptime().count(), 0.0);
}

TEST(Uvpp2System, copiesCpuAndInterfaceInformation) {
  auto cpus = uv::cpu_infos();
  EXPECT_FALSE(cpus.empty());

  if (!cpus.empty()) {
    EXPECT_GE(cpus.front().speed, 0);
  }

  auto interfaces = uv::interface_addresses();
  for (const auto &interface : interfaces) {
    EXPECT_FALSE(interface.name.empty());
    EXPECT_EQ(interface.physical_address.size(), 6u);
    EXPECT_TRUE(interface.address.is_v4() || interface.address.is_v6());
  }
}

TEST(Uvpp2System, reportsMemoryAndParallelism) {
  EXPECT_GT(uv::total_memory(), 0u);
  (void)uv::free_memory();
  EXPECT_GT(uv::resident_set_memory(), 0u);

#if UVPP_HAS_AVAILABLE_PARALLELISM
  EXPECT_GT(uv::available_parallelism(), 0u);
#endif
}

TEST(Uvpp2System, reportsAndChangesDirectories) {
#if UVPP_HAS_OS_TMPDIR
  EXPECT_FALSE(uv::tmpdir().empty());
#endif

#if UVPP_HAS_OS_HOMEDIR
  EXPECT_FALSE(uv::homedir().empty());
#endif

  auto original = uv::cwd();
  ASSERT_FALSE(original.empty());

#if UVPP_HAS_OS_TMPDIR
  auto target = uv::tmpdir();
#else
  auto target = std::filesystem::temp_directory_path().string();
#endif

  uv::chdir(target);
  EXPECT_EQ(std::filesystem::canonical(uv::cwd()), std::filesystem::canonical(target));
  uv::chdir(original);
  EXPECT_EQ(std::filesystem::canonical(uv::cwd()), std::filesystem::canonical(original));
}

TEST(Uvpp2System, reportsExecutablePath) {
  EXPECT_FALSE(uv::exepath().empty());
}

TEST(Uvpp2System, sleepBlockingForAcceptsChronoDuration) {
#if UVPP_HAS_SLEEP
  auto start = std::chrono::steady_clock::now();
  uv::sleep_blocking_for(std::chrono::milliseconds{1});
  EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds{1});
#else
  GTEST_SKIP() << "uv_sleep is unavailable in this libuv version";
#endif
}
