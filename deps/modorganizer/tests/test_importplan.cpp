#include <gtest/gtest.h>

#include "modlists/importplan.h"

using namespace modlists;

namespace
{
ModEntry nexusEntry(const QString& name, int modId, std::vector<int> fileIds)
{
  ModEntry e;
  e.name = name;
  for (int f : fileIds) {
    Source s;
    s.kind   = SourceKind::Nexus;
    s.modId  = modId;
    s.fileId = f;
    e.sources.push_back(s);
  }
  return e;
}

using Names = std::vector<QString>;
}  // namespace

TEST(ImportPlan, Classify)
{
  const std::vector<InstalledMod> installed{{"SkyUI", 12604, {35407, 35408}, "5.2"}};

  EXPECT_EQ(classify(nexusEntry("SkyUI", 12604, {35407}), installed,
                     Destination::ExistingProfile),
            RowStatus::AlreadyInstalled);
  EXPECT_EQ(classify(nexusEntry("SkyUI", 12604, {35407, 35408}), installed,
                     Destination::NewProfile),
            RowStatus::AlreadyInstalled);
  EXPECT_EQ(classify(nexusEntry("SkyUI", 12604, {99999}), installed,
                     Destination::ExistingProfile),
            RowStatus::DifferentVersion);
  EXPECT_EQ(classify(nexusEntry("SkyUI", 12604, {35407, 99999}), installed,
                     Destination::ExistingProfile),
            RowStatus::DifferentVersion);
  EXPECT_EQ(classify(nexusEntry("Other", 1, {2}), installed,
                     Destination::ExistingProfile),
            RowStatus::New);

  // nothing is installed in a new instance: DifferentVersion becomes New
  EXPECT_EQ(classify(nexusEntry("SkyUI", 12604, {99999}), installed,
                     Destination::NewInstance),
            RowStatus::New);

  ModEntry manual;
  manual.name = "M";
  Source ms;
  ms.kind = SourceKind::Manual;
  manual.sources.push_back(ms);
  EXPECT_EQ(classify(manual, installed, Destination::NewProfile), RowStatus::Manual);

  ModEntry bundled;
  bundled.name = "B";
  Source bs;
  bs.kind = SourceKind::Bundled;
  bundled.sources.push_back(bs);
  EXPECT_EQ(classify(bundled, installed, Destination::NewProfile), RowStatus::Bundled);

  ModEntry sep;
  sep.name        = "Visuals";
  sep.isSeparator = true;
  EXPECT_EQ(classify(sep, installed, Destination::ExistingProfile), RowStatus::New);

  // a mixed entry follows its first source
  ModEntry mixed = nexusEntry("SkyUI", 12604, {35407});
  mixed.sources.push_back(ms);
  EXPECT_EQ(classify(mixed, installed, Destination::ExistingProfile),
            RowStatus::AlreadyInstalled);
}

TEST(ImportPlan, InstallName)
{
  ModEntry e;
  e.name = "SkyUI";

  std::set<QString, CaseInsensitive> names;
  EXPECT_EQ(installName(e, "Minha Lista", names), "SkyUI");

  names.insert("skyui");
  EXPECT_EQ(installName(e, "Minha Lista", names), "SkyUI [Minha Lista]");

  names.insert("SkyUI [Minha Lista]");
  EXPECT_EQ(installName(e, "Minha Lista", names), "SkyUI [Minha Lista] (2)");

  names.insert("SkyUI [Minha Lista] (2)");
  EXPECT_EQ(installName(e, "Minha Lista", names), "SkyUI [Minha Lista] (3)");
}

TEST(ImportPlan, MergeModOrderExisting)
{
  const Names user{"A", "B"};
  const Names plan{"X", "B", "Y"};

  auto r = mergeModOrder(user, plan, {}, Destination::ExistingProfile, false);
  EXPECT_EQ(r.order, (Names{"A", "B", "X", "Y"}));
  EXPECT_TRUE(r.disabled.empty());

  r = mergeModOrder(user, plan, {}, Destination::ExistingProfile, true);
  EXPECT_EQ(r.order, (Names{"A", "X", "B", "Y"}));

  r = mergeModOrder(user, plan, {"A"}, Destination::ExistingProfile, false);
  EXPECT_EQ(r.disabled, (Names{"A"}));
}

TEST(ImportPlan, MergeModOrderNew)
{
  const auto r =
      mergeModOrder({"A", "B"}, {"X", "B", "Y"}, {}, Destination::NewProfile, false);
  EXPECT_EQ(r.order, (Names{"X", "B", "Y"}));
  EXPECT_EQ(r.disabled, (Names{"A"}));
}

TEST(ImportPlan, MergePluginsNew)
{
  const std::vector<PluginEntry> plan{{"a.esm", true}, {"b.esp", true}, {"c.esp", false}};
  const std::set<QString, CaseInsensitive> available{"a.esm", "C.esp"};

  const auto r = mergePluginOrder({}, plan, available, Destination::NewProfile);
  ASSERT_EQ(r.order.size(), 2u);
  EXPECT_EQ(r.order[0].name, "a.esm");
  EXPECT_EQ(r.order[1].name, "c.esp");
  EXPECT_FALSE(r.order[1].enabled);
  EXPECT_EQ(r.notInstalled, (Names{"b.esp"}));
}

TEST(ImportPlan, MergePluginsExisting)
{
  const std::vector<PluginEntry> user{{"u.esp", true}, {"a.esm", true}};
  const std::vector<PluginEntry> plan{{"a.esm", true}, {"b.esp", true}};
  const std::set<QString, CaseInsensitive> available{"a.esm", "b.esp", "u.esp"};

  const auto r = mergePluginOrder(user, plan, available, Destination::ExistingProfile);
  ASSERT_EQ(r.order.size(), 3u);
  EXPECT_EQ(r.order[0].name, "u.esp");
  EXPECT_EQ(r.order[1].name, "a.esm");
  EXPECT_EQ(r.order[2].name, "b.esp");
  EXPECT_FALSE(r.userOrderChanged);
}

TEST(ImportPlan, ReuseDownload)
{
  int calls = 0;
  auto md5Of = [&](const QString& path) {
    ++calls;
    return path == "/d/other.7z" ? QString("abc") : QString("zzz");
  };

  Source s;
  s.modId  = 10;
  s.fileId = 20;

  // same modId/fileId, no md5 in the plan: reused without hashing
  std::vector<DownloadCandidate> c{{"/d/a.7z", 10, 20, 5}};
  EXPECT_EQ(findReusableDownload(s, c, md5Of), QString("/d/a.7z"));
  EXPECT_EQ(calls, 0);

  // same ids but a different md5: not reused
  s.md5 = "abc";
  EXPECT_FALSE(findReusableDownload(s, c, md5Of).has_value());
  EXPECT_EQ(calls, 1);

  // only the md5 matches (ids unknown), same size
  s.size = 100;
  c      = {{"/d/other.7z", 0, 0, 100}};
  calls  = 0;
  EXPECT_EQ(findReusableDownload(s, c, md5Of), QString("/d/other.7z"));
  EXPECT_EQ(calls, 1);

  // a different size: md5Of is not called
  c     = {{"/d/other.7z", 0, 0, 101}};
  calls = 0;
  EXPECT_FALSE(findReusableDownload(s, c, md5Of).has_value());
  EXPECT_EQ(calls, 0);
}

TEST(ImportPlan, FilePageUrl)
{
  EXPECT_EQ(nexusFilePageUrl("skyrimspecialedition", 266, 1000).toString(),
            "https://www.nexusmods.com/skyrimspecialedition/mods/266?tab=files&file_id=1000");
}

TEST(ImportPlan, InstallNameSanitizes)
{
  std::set<QString, CaseInsensitive> none;
  auto name = [&](const QString& n, const QString& list = "L",
                  const std::set<QString, CaseInsensitive>& ex = {}) {
    ModEntry e;
    e.name = n;
    return installName(e, list, ex);
  };

  const QString evil = name("../evil");
  EXPECT_FALSE(evil.contains('/'));
  EXPECT_FALSE(evil.startsWith('.'));
  EXPECT_EQ(name("a/b"), "a_b");
  EXPECT_EQ(name(".."), "Unnamed mod");
  EXPECT_EQ(name("."), "Unnamed mod");
  EXPECT_EQ(name(""), "Unnamed mod");
  EXPECT_EQ(name(QString("a") + QChar(0) + "b\n"), "a_b_");
  EXPECT_EQ(name("a\\b:c*d?e\"f<g>h|i"), "a_b_c_d_e_f_g_h_i");
  EXPECT_EQ(name(" x. "), "x");
  EXPECT_EQ(name("SkyUI", "x/y", {"SkyUI"}), "SkyUI [x_y]");
}

TEST(ImportPlan, ClassifyWithoutFileIds)
{
  const std::vector<InstalledMod> installed{{"SkyUI", 12604, {35407}, "5.2"}};
  EXPECT_EQ(classify(nexusEntry("SkyUI", 12604, {0}), installed,
                     Destination::ExistingProfile),
            RowStatus::DifferentVersion);
}

TEST(ImportPlan, MergeModOrderRepeatedPlanName)
{
  const Names plan{"X", "Y", "x"};
  EXPECT_EQ(mergeModOrder({"A"}, plan, {}, Destination::NewProfile, false).order,
            (Names{"X", "Y"}));
  EXPECT_EQ(mergeModOrder({"A"}, plan, {}, Destination::ExistingProfile, false).order,
            (Names{"A", "X", "Y"}));
  EXPECT_EQ(mergeModOrder({"A"}, plan, {}, Destination::ExistingProfile, true).order,
            (Names{"A", "X", "Y"}));
}

TEST(ImportPlan, AllowedImportedEnv)
{
  for (const char* name :
       {"DXVK_HUD", "dxvk_async", "VKD3D_CONFIG", "PROTON_LOG", "proton_use_wined3d",
        "PROTON_ENABLE_NVAPI", "PROTON_NO_ESYNC", "PROTON_NO_FSYNC",
        "PROTON_HIDE_NVIDIA_GPU", "PROTON_ENABLE_WAYLAND", "MANGOHUD", "mangohud",
        "WINEDEBUG", "WINEESYNC", "WINEFSYNC", "RADV_PERFTEST", "mesa_glthread",
        "__GL_SHADER_DISK_CACHE", "__GL_SHADER_DISK_CACHE_SKIP_CLEANUP",
        "ENABLE_VKBASALT"}) {
    EXPECT_TRUE(isAllowedImportedEnv(name, "1")) << name;
  }
  for (const char* name :
       {"PROTONPATH", "WINEPREFIX", "WINELOADER", "WINEDLLPATH",
        "STEAM_COMPAT_DATA_PATH", "VK_ICD_FILENAMES", "LD_PRELOAD", "PATH", "GAMEID",
        "SteamAppId", "PROTON_REMOTE_DEBUG_CMD", "PROTON_LOG_DIR", "DXVK_CONFIG_FILE",
        "DXVK_LOG_PATH", "VKD3D_SHADER_CACHE_PATH", "DXVK_STATE_CACHE_DIR",
        "MANGOHUD_CONFIG", "MANGOHUD_DLSYM", "XDG_DATA_DIRS", "PYTHONPATH", "", "DXVK"}) {
    EXPECT_FALSE(isAllowedImportedEnv(name, "1")) << name;
  }

  EXPECT_TRUE(isAllowedImportedEnv("WINEDLLOVERRIDES", "dinput8=n,b"));
  EXPECT_TRUE(isAllowedImportedEnv("WINEDLLOVERRIDES", "d3d11,dxgi=n;xinput1_3=b;"));
  EXPECT_TRUE(isAllowedImportedEnv("WINEDLLOVERRIDES", "winemenubuilder.exe=d"));
  EXPECT_TRUE(isAllowedImportedEnv("WINEDLLOVERRIDES", "mscoree="));
  EXPECT_FALSE(isAllowedImportedEnv("WINEDLLOVERRIDES", "/tmp/x.dll=n"));
  EXPECT_FALSE(isAllowedImportedEnv("WINEDLLOVERRIDES", "C:\\x.dll=n"));
  EXPECT_FALSE(isAllowedImportedEnv("WINEDLLOVERRIDES", ""));

  // the value of any allowed name: no control character, path or marker
  EXPECT_FALSE(isAllowedImportedEnv("DXVK_HUD", "fps\nLD_PRELOAD=/x"));
  EXPECT_FALSE(isAllowedImportedEnv("DXVK_HUD", "fps\r"));
  EXPECT_FALSE(isAllowedImportedEnv("DXVK_HUD", QString("fps") + QChar(0)));
  EXPECT_FALSE(isAllowedImportedEnv("DXVK_HUD", "fps\t1"));
  EXPECT_TRUE(isAllowedImportedEnv("DXVK_HUD", "fps,devinfo"));
  EXPECT_TRUE(isAllowedImportedEnv("PROTON_LOG", "1"));
  EXPECT_FALSE(isAllowedImportedEnv("DXVK_HUD", "%GAME%"));
  EXPECT_TRUE(isAllowedImportedEnv("VKD3D_CONFIG", "dxr"));
  EXPECT_FALSE(isAllowedImportedEnv("VKD3D_CONFIG", "/x"));
  EXPECT_FALSE(isAllowedImportedEnv("RADV_PERFTEST", "C:x"));
  EXPECT_FALSE(isAllowedImportedEnv("WINEDEBUG", "a\\b"));
}
