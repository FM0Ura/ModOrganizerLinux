#include <gtest/gtest.h>

#include "smoke.h"

TEST(SmokeTest, RunReturnsOk)
{
  const auto result = smoke::run();
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, "ok");
}
