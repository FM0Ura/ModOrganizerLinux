#include <gtest/gtest.h>

#include "modlists/importsummary.h"

using namespace modlists;

namespace
{

SummaryRow row(const QString& mod, Outcome outcome, const QString& detail = {})
{
  SummaryRow r;
  r.mod     = mod;
  r.outcome = outcome;
  r.detail  = detail;
  return r;
}

}  // namespace

TEST(ImportSummary, TextHasEverySection)
{
  Summary s;
  s.rows = {row("Alpha Mod", Outcome::Installed),
            row("Beta Mod", Outcome::Reused, "Beta (mine)"),
            row("Gamma Mod", Outcome::Failed, "Checksum mismatch"),
            row("Delta Mod", Outcome::Skipped, "Skipped by the user")};
  s.pluginsNotInstalled      = {"Missing.esp"};
  s.missingMasters           = {{"Child.esp", QStringList{"Parent.esm", "Other.esm"}}};
  s.inisNotApplied           = {"Skyrim.ini"};
  s.executablesWithoutTarget = {"SKSE"};
  s.executablesAdded         = {"xEdit [My List]: /games/sse/xEdit.exe -quickautoclean"};
  s.offerLoot                = true;
  s.userPluginOrderChanged   = true;

  const QString text = summaryText(s);

  for (const char* section :
       {"Installed", "Reused", "Failed", "Skipped", "Plugins not installed",
        "Missing masters", "INI files not applied", "Executables without a target",
        "Executables added"}) {
    EXPECT_TRUE(text.contains(section)) << section;
  }
  for (const char* mod : {"Alpha Mod", "Beta Mod", "Gamma Mod", "Delta Mod"}) {
    EXPECT_TRUE(text.contains(mod)) << mod;
  }
  EXPECT_TRUE(text.contains("Checksum mismatch"));
  EXPECT_TRUE(text.contains("Missing.esp"));
  EXPECT_TRUE(text.contains("Child.esp"));
  EXPECT_TRUE(text.contains("Parent.esm"));
  EXPECT_TRUE(text.contains("Other.esm"));
  EXPECT_TRUE(text.contains("Skyrim.ini"));
  EXPECT_TRUE(text.contains("SKSE"));
  EXPECT_TRUE(text.contains("xEdit [My List]: /games/sse/xEdit.exe -quickautoclean"));
  EXPECT_TRUE(text.contains("LOOT"));
  EXPECT_TRUE(text.contains("plugin order"));
}

TEST(ImportSummary, EmptySectionsAreLeftOut)
{
  Summary s;
  s.rows = {row("Alpha Mod", Outcome::Installed)};

  const QString text = summaryText(s);
  EXPECT_TRUE(text.contains("Alpha Mod"));
  EXPECT_FALSE(text.contains("Failed"));
  EXPECT_FALSE(text.contains("Missing masters"));
  EXPECT_FALSE(text.contains("LOOT"));
  EXPECT_FALSE(text.contains("Executables added"));
}

TEST(ImportSummary, RetryableOnlyFailed)
{
  Summary s;
  s.rows = {row("A", Outcome::Installed), row("B", Outcome::Failed, "Download failed"),
            row("C", Outcome::Reused), row("D", Outcome::Skipped),
            row("E", Outcome::Failed, "Checksum mismatch"),
            row("F", Outcome::Failed, "not in the map")};

  const std::map<QString, int> entryOfMod = {{"A", 0}, {"B", 1}, {"C", 2},
                                             {"D", 3}, {"E", 4}};

  EXPECT_EQ(retryable(s, entryOfMod), (std::vector<int>{1, 4}));
}

TEST(ImportSummary, CanInstallManually)
{
  EXPECT_TRUE(canInstallManually("FOMOD without recorded choices"));
  EXPECT_TRUE(canInstallManually("Needs a manual installation"));
  EXPECT_TRUE(canInstallManually("FOMOD page \"Options\" no longer exists"));
  EXPECT_FALSE(canInstallManually("Checksum mismatch"));
  EXPECT_FALSE(canInstallManually(""));

  EXPECT_TRUE(failedRow("X", "Needs a manual installation").canInstallManually);
  EXPECT_FALSE(failedRow("X", "Checksum mismatch").canInstallManually);
  EXPECT_EQ(failedRow("X", "Checksum mismatch").outcome, Outcome::Failed);
}
