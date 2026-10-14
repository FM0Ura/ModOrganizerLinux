#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <csignal>
#include <fstream>
#include <sstream>
#include <string>

#include "envdump.h"
#include "tempdir.h"

namespace
{

// no core file for the crashes of the tests
void disableCoreFiles()
{
  rlimit rl = {};
  ::setrlimit(RLIMIT_CORE, &rl);
}

std::string slurp(const std::filesystem::path& p)
{
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST(EnvDumpDeathTest, CrashLogsBacktraceAndKeepsTheSignal)
{
  // forks without re-running the test, so that the child writes to the same log
  GTEST_FLAG_SET(death_test_style, "fast");

  TempDir tmp;
  const auto log = tmp.path() / "crash.log";

  // the process is ended by the original signal, after the backtrace is written to
  // stderr and to the crash log
  EXPECT_EXIT(
      {
        disableCoreFiles();
        env::setCrashHandlers(log);
        std::raise(SIGSEGV);
      },
      testing::KilledBySignal(SIGSEGV),
      "ModOrganizer has crashed \\(signal 11\\), backtrace:.*organizer_tests");

  const auto contents = slurp(log);
  EXPECT_NE(contents.find("ModOrganizer has crashed (signal 11), backtrace:\n"),
            std::string::npos)
      << contents;
  EXPECT_NE(contents.find("organizer_tests"), std::string::npos) << contents;

  EXPECT_EXIT(
      {
        disableCoreFiles();
        env::setCrashHandlers();
        std::abort();
      },
      testing::KilledBySignal(SIGABRT), "ModOrganizer has crashed \\(signal 6\\)");

  EXPECT_EXIT(
      {
        disableCoreFiles();
        env::setCrashHandlers();
        std::raise(SIGFPE);
      },
      testing::KilledBySignal(SIGFPE), "ModOrganizer has crashed \\(signal 8\\)");
}

TEST(EnvDumpDeathTest, BlockedHandlerIsEndedByTheWatchdog)
{
  GTEST_FLAG_SET(death_test_style, "fast");

  TempDir tmp;
  const auto fifo = tmp.path() / "crash.fifo";
  ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);

  // the crash log is a full pipe that nobody reads: writing the backtrace to it
  // blocks, the watchdog ends the process with the original signal
  EXPECT_EXIT(
      {
        disableCoreFiles();

        const int reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
        const int writer = ::open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
        if (reader == -1 || writer == -1) {
          std::_Exit(1);
        }

        const char buffer[4096] = {};
        while (::write(writer, buffer, sizeof(buffer)) > 0) {
        }

        env::setCrashHandlers(fifo);
        std::raise(SIGSEGV);
      },
      testing::KilledBySignal(SIGSEGV),
      "ModOrganizer has crashed \\(signal 11\\).*crash handler timed out");
}
