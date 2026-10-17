// FILETIME <-> timespec conversions (uibase/linux/filetime.h). Only built on Linux.

#include <gtest/gtest.h>

#include <uibase/linux/filetime.h>

using namespace MOBase::linux;

TEST(LinuxFileTime, UnixEpoch)
{
  const FILETIME ft = toFileTime(timespec{0, 0});

  EXPECT_EQ(fileTimeTicks(ft), 116444736000000000ULL);

  const timespec t = toTimespec(ft);
  EXPECT_EQ(t.tv_sec, 0);
  EXPECT_EQ(t.tv_nsec, 0);
}

TEST(LinuxFileTime, SplitsInTwoDwords)
{
  const FILETIME ft = toFileTime(timespec{0, 0});

  EXPECT_EQ(ft.dwHighDateTime, 116444736000000000ULL >> 32);
  EXPECT_EQ(ft.dwLowDateTime, 116444736000000000ULL & 0xffffffff);
}

TEST(LinuxFileTime, NanosecondsAreTruncatedTo100ns)
{
  const FILETIME ft = toFileTime(timespec{10, 123'456'789});

  EXPECT_EQ(fileTimeTicks(ft), 116444736000000000ULL + 10 * 10'000'000ULL + 1'234'567);

  const timespec t = toTimespec(ft);
  EXPECT_EQ(t.tv_sec, 10);
  EXPECT_EQ(t.tv_nsec, 123'456'700);
}

TEST(LinuxFileTime, RoundTripOfExactTimes)
{
  for (const timespec in : {timespec{1, 100}, timespec{1'700'000'000, 999'999'900},
                            timespec{4'102'444'800, 0}}) {
    const timespec out = toTimespec(toFileTime(in));
    EXPECT_EQ(out.tv_sec, in.tv_sec);
    EXPECT_EQ(out.tv_nsec, in.tv_nsec);
  }
}

TEST(LinuxFileTime, BeforeUnixEpoch)
{
  // 1969-12-31 23:59:59.5
  const FILETIME ft = toFileTime(timespec{-1, 500'000'000});
  EXPECT_EQ(fileTimeTicks(ft), 116444736000000000ULL - 5'000'000);

  const timespec t = toTimespec(ft);
  EXPECT_EQ(t.tv_sec, -1);
  EXPECT_EQ(t.tv_nsec, 500'000'000);

  // 1601-01-01, the FILETIME epoch
  const FILETIME zero = toFileTime(timespec{-11'644'473'600, 0});
  EXPECT_EQ(fileTimeTicks(zero), 0u);

  const timespec back = toTimespec(zero);
  EXPECT_EQ(back.tv_sec, -11'644'473'600);
  EXPECT_EQ(back.tv_nsec, 0);
}
