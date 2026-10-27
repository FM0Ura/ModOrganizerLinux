#include "../src/nexuspage.h"

#include <gtest/gtest.h>

using namespace nexuspage;

TEST(NexusPage, CacheFileExtension)
{
  EXPECT_EQ(cacheFileName("skyrimspecialedition", 42, "https://x/a/b.PNG?v=2"),
            "skyrimspecialedition_42.png");
  EXPECT_EQ(cacheFileName("skyrimspecialedition", 42, "https://x/a/image"),
            "skyrimspecialedition_42.jpg");
  EXPECT_EQ(cacheFileName("skyrimspecialedition", 42, "https://x/a/pic.webp"),
            "skyrimspecialedition_42.webp");
  EXPECT_EQ(cacheFileName("skyrimspecialedition", 42, "https://x/a/f.exe"),
            "skyrimspecialedition_42.jpg");
}

TEST(NexusPage, CacheFileGameNameSeparators)
{
  EXPECT_EQ(cacheFileName("a/b", 1, "https://x/p.png"), "a_b_1.png");
  EXPECT_EQ(cacheFileName("a\\b", 1, "https://x/p.png"), "a_b_1.png");
}

TEST(NexusPage, NeedsImageDownload)
{
  EXPECT_TRUE(needsImageDownload("u", "", false));
  EXPECT_FALSE(needsImageDownload("u", "u", true));
  EXPECT_TRUE(needsImageDownload("u2", "u", true));
  EXPECT_FALSE(needsImageDownload("", "", false));
}

TEST(NexusPage, HtmlWithAndWithoutPicture)
{
  const QString with = pageHtml("file:///c/x.jpg", "<h1>H</h1>", "<p>B</p>");
  EXPECT_TRUE(with.contains("<img src=\"file:///c/x.jpg\""));
  EXPECT_TRUE(with.contains("<h1>H</h1>"));
  EXPECT_TRUE(with.contains("<p>B</p>"));
  EXPECT_FALSE(with.contains("max-width: 1060px"));

  EXPECT_FALSE(pageHtml("", "<h1>H</h1>", "<p>B</p>").contains("<img"));
}

TEST(NexusPage, HtmlKeepsPercentPlaceholders)
{
  const QString html = pageHtml("", "%1", "a %1 b %2");
  EXPECT_TRUE(html.contains("a %1 b %2"));
  EXPECT_TRUE(html.contains("<body>%1a %1 b %2</body>"));
}

TEST(NexusPage, DescriptionDocumentKeepsCss)
{
  const QString html = descriptionDocument("<p>x</p>");
  EXPECT_TRUE(html.contains("max-width: 1060px"));
  EXPECT_TRUE(html.contains("<body><p>x</p></body>"));
}

TEST(NexusPage, StatusText)
{
  const QDateTime now(QDate(2026, 10, 27), QTime(12, 0));
  const QDateTime modified(QDate(2026, 10, 27), QTime(8, 0));
  const QDateTime query(QDate(2026, 10, 26), QTime(8, 0));

  const QString text = statusText(modified, query, now);
  EXPECT_TRUE(text.startsWith("Updated on Nexus today"));
  EXPECT_TRUE(text.endsWith("checked yesterday"));
  EXPECT_TRUE(statusText(modified, QDateTime(), now).endsWith("checked never"));
}

TEST(NexusPage, PanelTag)
{
  EXPECT_TRUE(isPanelRequest(panelRequestTag()));
  EXPECT_FALSE(isPanelRequest(QVariant()));
  EXPECT_FALSE(isPanelRequest(QVariant(1)));
}
