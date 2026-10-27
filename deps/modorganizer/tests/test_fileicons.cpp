#include <gtest/gtest.h>

#include "fileicons.h"

using namespace fileicons;

TEST(FileIcons, GameTypes)
{
  auto esp = gameFileIcon(u"esp");
  ASSERT_TRUE(esp);
  EXPECT_EQ(esp->themeNames, QStringList{"plugins"});
  EXPECT_EQ(esp->fallbackResource, ":/MO/gui/plugins");

  auto bsa = gameFileIcon(u"bsa");
  ASSERT_TRUE(bsa);
  EXPECT_EQ(bsa->themeNames, QStringList{"application-x-archive"});
  EXPECT_EQ(bsa->fallbackResource, ":/MO/gui/package");

  auto nif = gameFileIcon(u"nif");
  ASSERT_TRUE(nif);
  EXPECT_EQ(nif->themeNames, QStringList{"application-x-blender"});
  EXPECT_EQ(nif->fallbackResource, "");

  auto ini = gameFileIcon(u"ini");
  ASSERT_TRUE(ini);
  EXPECT_EQ(ini->themeNames, (QStringList{"configure", "settings-configure"}));

  auto dll = gameFileIcon(u"dll");
  ASSERT_TRUE(dll);
  EXPECT_EQ(dll->themeNames, QStringList{"application-x-sharedlib"});

  auto utoc = gameFileIcon(u"utoc");
  ASSERT_TRUE(utoc);
  EXPECT_EQ(utoc->themeNames, QStringList{"package-x-generic"});
  EXPECT_EQ(utoc->fallbackResource, ":/MO/gui/package");

  EXPECT_FALSE(gameFileIcon(u"txt"));
}

TEST(FileIcons, CaseInsensitive)
{
  auto upper = gameFileIcon(u"ESP");
  auto lower = gameFileIcon(u"esp");
  ASSERT_TRUE(upper);
  ASSERT_TRUE(lower);
  EXPECT_EQ(upper->themeNames, lower->themeNames);
  EXPECT_EQ(upper->fallbackResource, lower->fallbackResource);

  auto dds = gameFileIcon(u"Dds");
  ASSERT_TRUE(dds);
  EXPECT_EQ(dds->themeNames, gameFileIcon(u"dds")->themeNames);
}

TEST(FileIcons, MimeNames)
{
  const QStringList txt = mimeIconNames(u"txt");
  EXPECT_FALSE(txt.isEmpty());
  EXPECT_TRUE(txt.contains("text-plain") || txt.contains("text-x-generic"));

  bool image = false;
  for (const QString& n : mimeIconNames(u"png")) {
    image = image || n.startsWith("image-");
  }
  EXPECT_TRUE(image);
}

TEST(FileIcons, UnknownExtensionHasNoMimeIcon)
{
  EXPECT_TRUE(mimeIconNames(u"xyz123").isEmpty());
}
