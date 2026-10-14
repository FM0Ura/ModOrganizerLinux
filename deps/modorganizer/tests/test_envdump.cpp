#include <gtest/gtest.h>

#include <sys/resource.h>

#include <csignal>

#include "envdump.h"

namespace
{

// no core file for the crashes of the tests
void disableCoreFiles()
{
  rlimit rl = {};
  ::setrlimit(RLIMIT_CORE, &rl);
}

}  // namespace

TEST(EnvDumpDeathTest, CrashLogsBacktraceAndKeepsTheSignal)
{
  GTEST_FLAG_SET(death_test_style, "threadsafe");

  EXPECT_EXIT(
      {
        disableCoreFiles();
        env::setCrashHandlers();
        std::raise(SIGSEGV);
      },
      testing::KilledBySignal(SIGSEGV),
      "ModOrganizer has crashed \\(signal 11\\), backtrace:.*organizer_tests");

  EXPECT_EXIT(
      {
        disableCoreFiles();
        env::setCrashHandlers();
        std::abort();
      },
      testing::KilledBySignal(SIGABRT), "ModOrganizer has crashed \\(signal 6\\)");
}
