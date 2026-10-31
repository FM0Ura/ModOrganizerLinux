#include <gtest/gtest.h>

#include "modlists/installorder.h"

using namespace modlists;

TEST(InstallOrder, FileKeyFromIds)
{
  EXPECT_EQ(fileKey(12, 34), "12_34");
}

TEST(InstallOrder, FileKeyFromArchiveName)
{
  const QString key = fileKey(QStringLiteral("a.7z"));
  EXPECT_TRUE(key.startsWith("archive_")) << key.toStdString();
  EXPECT_EQ(key, fileKey(QStringLiteral("a.7z")));
  EXPECT_EQ(key.size(), QStringLiteral("archive_").size() + 12);
  EXPECT_NE(key, fileKey(QStringLiteral("b.7z")));
}

TEST(InstallOrder, FileKeyUnknownIdUsesArchiveName)
{
  EXPECT_EQ(fileKey(0, 34, QStringLiteral("a.7z")), fileKey(QStringLiteral("a.7z")));
  EXPECT_EQ(fileKey(12, 0, QStringLiteral("a.7z")), fileKey(QStringLiteral("a.7z")));
  EXPECT_EQ(fileKey(12, 34, QStringLiteral("a.7z")), "12_34");
}

TEST(InstallOrder, AppendMovesExistingKeyToEnd)
{
  EXPECT_EQ(appendInstallOrder({"1_1", "2_2"}, "1_1"), (QStringList{"2_2", "1_1"}));
}

TEST(InstallOrder, AppendToEmpty)
{
  EXPECT_EQ(appendInstallOrder({}, "3_3"), (QStringList{"3_3"}));
}

TEST(InstallOrder, FomodChoicesKey)
{
  EXPECT_EQ(fomodChoicesKey("12_34"), "choices_12_34");
}
