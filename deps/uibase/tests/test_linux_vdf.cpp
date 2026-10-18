// Text VDF reader of uibase/linux/vdf.h. Only built on Linux, see tests/CMakeLists.txt.

#include <uibase/linux/vdf.h>

#include <gtest/gtest.h>

using namespace MOBase;

TEST(LinuxVdf, NestedBlocksAndValues)
{
  const auto root = vdf::parse(R"("AppState"
{
	"appid"		"489830"
	"installdir"		"Skyrim Special Edition"
	"UserConfig" { "language" "english" }
})");
  ASSERT_TRUE(root.block);
  EXPECT_EQ(root.path({u"appstate"})->value(u"InstallDir"), "Skyrim Special Edition");
  EXPECT_EQ(root.path({u"AppState", u"userconfig"})->value(u"language"), "english");
  EXPECT_EQ(root.path({u"AppState", u"missing"}), nullptr);
  EXPECT_EQ(root.child(u"AppState")->value(u"UserConfig"), "");
}

TEST(LinuxVdf, EscapesCommentsConditionalsAndBareWords)
{
  const auto root = vdf::parse("// comment\n\"a\" \"C:\\\\Games\\\\\\\"x\\\"\"\n"
                               "b bare [$WIN32]\n\"c\" \"tab\\tline\\n\"\n");
  EXPECT_EQ(root.value(u"a"), "C:\\Games\\\"x\"");
  EXPECT_EQ(root.value(u"b"), "bare");
  EXPECT_EQ(root.value(u"c"), "tab\tline\n");
}

TEST(LinuxVdf, RepeatedKeyLastWins)
{
  const auto root = vdf::parse(R"("k" "1" "K" "2")");
  EXPECT_EQ(root.value(u"k"), "2");
  EXPECT_EQ(root.children.size(), 2u);
}

TEST(LinuxVdf, MalformedKeepsWhatCameBefore)
{
  const auto root =
      vdf::parse("\"a\" \"1\" \"b\" { \"c\" \"2\" } \"d\" \"unterminated");
  EXPECT_EQ(root.value(u"a"), "1");
  EXPECT_EQ(root.path({u"b"})->value(u"c"), "2");
  EXPECT_EQ(root.child(u"d"), nullptr);

  EXPECT_TRUE(vdf::parse("}").children.empty());
  EXPECT_TRUE(vdf::parse("").children.empty());
}

TEST(LinuxVdf, DepthIsBounded)
{
  QByteArray deep;
  for (int i = 0; i < 100; ++i) {
    deep += "\"k\" { ";
  }
  const auto root = vdf::parse(deep);
  int depth       = 0;
  for (const auto* n = &root; n && !n->children.empty(); n = &n->children.front()) {
    ++depth;
  }
  EXPECT_LE(depth, 65);
}

TEST(LinuxVdf, ParseFile)
{
  EXPECT_FALSE(vdf::parseFile("/nonexistent/file.vdf"));
}
