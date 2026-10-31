#include <gtest/gtest.h>

#include "modlists/guidedmatch.h"

using namespace modlists;

namespace
{
GuidedQueue makeQueue()
{
  return GuidedQueue({{0, 0, 10, 100}, {1, 0, 20, 200}, {2, 0, 30, 300}});
}
}  // namespace

TEST(GuidedMatch, InOrder)
{
  auto q = makeQueue();
  EXPECT_EQ(q.total(), 3u);
  ASSERT_TRUE(q.current().has_value());
  EXPECT_EQ(*q.current(), 0u);
  EXPECT_EQ(q.match(10, 100), MatchResult::Current);
  EXPECT_EQ(*q.current(), 1u);
  EXPECT_EQ(q.match(20, 200), MatchResult::Current);
  EXPECT_EQ(q.done(), 2u);
  EXPECT_EQ(*q.current(), 2u);
  EXPECT_EQ(q.match(30, 300), MatchResult::Current);
  EXPECT_FALSE(q.current().has_value());
}

TEST(GuidedMatch, OtherItem)
{
  auto q = makeQueue();
  EXPECT_EQ(q.match(30, 300), MatchResult::OtherItem);
  EXPECT_EQ(*q.current(), 0u);
  EXPECT_EQ(q.items()[2].state, GuidedItem::Queued);
  EXPECT_EQ(q.done(), 1u);
}

TEST(GuidedMatch, DuplicateAndSkipped)
{
  auto q = makeQueue();
  EXPECT_EQ(q.match(10, 100), MatchResult::Current);
  EXPECT_EQ(q.match(10, 100), MatchResult::AlreadyQueued);
  EXPECT_EQ(q.done(), 1u);

  // current is item 1; skip it, then its nxm arrives
  q.skip();
  EXPECT_EQ(q.items()[1].state, GuidedItem::Skipped);
  EXPECT_EQ(q.match(20, 200), MatchResult::SkippedItem);
  EXPECT_EQ(q.items()[1].state, GuidedItem::Skipped);
  EXPECT_EQ(q.done(), 1u);
  EXPECT_EQ(*q.current(), 2u);

  q.unskip(1);
  EXPECT_EQ(q.items()[1].state, GuidedItem::Pending);
  EXPECT_EQ(*q.current(), 1u);
}

TEST(GuidedMatch, NotInList)
{
  auto q = makeQueue();
  EXPECT_EQ(q.match(99, 999), MatchResult::NotInList);
  EXPECT_EQ(q.done(), 0u);
  EXPECT_EQ(*q.current(), 0u);
}
