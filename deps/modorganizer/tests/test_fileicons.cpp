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
  EXPECT_EQ(nif->fallbackResource, ":/MO/gui/filetype_model");
  EXPECT_EQ(gameFileIcon(u"hkx")->fallbackResource, ":/MO/gui/filetype_model");

  auto pex = gameFileIcon(u"pex");
  ASSERT_TRUE(pex);
  EXPECT_EQ(pex->themeNames, QStringList{"text-x-script"});
  EXPECT_EQ(pex->fallbackResource, ":/MO/gui/filetype_script");

  auto dds = gameFileIcon(u"dds");
  ASSERT_TRUE(dds);
  EXPECT_EQ(dds->themeNames, QStringList{"image-x-generic"});
  EXPECT_EQ(dds->fallbackResource, ":/MO/gui/filetype_image");

  auto ini = gameFileIcon(u"ini");
  ASSERT_TRUE(ini);
  EXPECT_EQ(ini->themeNames, (QStringList{"configure", "settings-configure"}));
  EXPECT_EQ(ini->fallbackResource, ":/MO/gui/filetype_text");

  auto dll = gameFileIcon(u"dll");
  ASSERT_TRUE(dll);
  EXPECT_EQ(dll->themeNames, QStringList{"application-x-sharedlib"});
  EXPECT_EQ(dll->fallbackResource, ":/MO/gui/filetype_library");
  EXPECT_EQ(gameFileIcon(u"asi")->fallbackResource, ":/MO/gui/filetype_library");

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

TEST(FileIcons, EveryGameTypeHasFallback)
{
  for (const auto* ext : {u"esp", u"esm", u"esl", u"bsa", u"ba2", u"nif", u"kf", u"hkx",
                          u"pex", u"psc", u"dds", u"ini", u"dll", u"asi", u"pak",
                          u"utoc", u"ucas"}) {
    const auto icon = gameFileIcon(QStringView(ext));
    ASSERT_TRUE(icon) << QString::fromUtf16(ext).toStdString();
    EXPECT_FALSE(icon->fallbackResource.isEmpty())
        << QString::fromUtf16(ext).toStdString();
  }
}

TEST(FileIcons, MimeFallback)
{
  EXPECT_EQ(mimeFallbackResource(u"png"), ":/MO/gui/filetype_image");
  EXPECT_EQ(mimeFallbackResource(u"txt"), ":/MO/gui/filetype_text");
  EXPECT_EQ(mimeFallbackResource(u"zip"), ":/MO/gui/filetype_archive");
  EXPECT_EQ(mimeFallbackResource(u"7z"), ":/MO/gui/filetype_archive");
  EXPECT_TRUE(mimeFallbackResource(u"xyz123").isEmpty());
}
