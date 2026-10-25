#include <gtest/gtest.h>
#include <uibase/versioning.h>

// the port appends its own number to the upstream version (2.5.2.N)
TEST(VersionOrder, PortNumberOrdersAfterUpstream)
{
  using MOBase::Version;
  const auto v = [](const char* s) {
    return Version::parse(s, Version::ParseMode::MO2);
  };
  EXPECT_LT(v("2.5.2"), v("2.5.2.1"));
  EXPECT_LT(v("2.5.2.1"), v("2.5.2.2"));
  EXPECT_LT(v("2.5.2.9"), v("2.5.2.10"));
  EXPECT_LT(v("2.5.2.10"), v("2.5.3.1"));
  EXPECT_EQ(v("2.5.2.1").string(), "2.5.2.1");
}
