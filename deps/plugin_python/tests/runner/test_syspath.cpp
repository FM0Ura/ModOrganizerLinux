#include <gtest/gtest.h>

#include "syspath.h"

using mo2::python::filterSysPath;

namespace {
    const std::vector<std::string> roots{"/usr/lib64/python3.14",
                                         "/usr/lib64/python3.14/site-packages",
                                         "/usr/lib/python3.14/site-packages"};
}

TEST(SysPath, KeepsTheSystemAndAddsModOrganizer)
{
    const std::vector<std::string> sysPath{
        "/usr/lib64/python314.zip",
        "/usr/lib64/python3.14",
        "/usr/lib64/python3.14/lib-dynload",
        "/home/u/.local/lib/python3.14/site-packages",
        "/usr/local/lib64/python3.14/site-packages",
        "/usr/local/lib/python3.14/site-packages",
        "/usr/lib64/python3.14/site-packages",
        "/usr/lib/python3.14/site-packages",
        "/home/u/pythonpath"};

    EXPECT_EQ(
        filterSysPath(sysPath, roots,
                      {"/mo2/plugins/plugin_python/libs", "/mo2/plugins/data"}),
        (std::vector<std::string>{
            "/usr/lib64/python3.14", "/usr/lib64/python3.14/lib-dynload",
            "/usr/lib64/python3.14/site-packages", "/usr/lib/python3.14/site-packages",
            "/mo2/plugins/plugin_python/libs", "/mo2/plugins/data"}));
}

TEST(SysPath, APrefixOfARootIsNotUnderIt)
{
    EXPECT_TRUE(filterSysPath({"/usr/lib64/python3.14-extra"}, roots, {}).empty());
    EXPECT_TRUE(filterSysPath({"/usr/lib64"}, roots, {}).empty());
}

TEST(SysPath, NoDuplicateOfAnExtraPath)
{
    EXPECT_EQ(
        filterSysPath({"/usr/lib64/python3.14"}, roots, {"/mo2/libs", "/mo2/libs"}),
        (std::vector<std::string>{"/usr/lib64/python3.14", "/mo2/libs"}));
}

TEST(SysPath, TrailingSlashOfARoot)
{
    EXPECT_EQ(filterSysPath({"/usr/lib/python3.14/site-packages/foo"},
                            {"/usr/lib/python3.14/site-packages/"}, {}),
              (std::vector<std::string>{"/usr/lib/python3.14/site-packages/foo"}));
}

// the paths are UTF-8 bytes, never converted through the C++ locale: a home folder with
// accents (Linux)
TEST(SysPath, NonAsciiPaths)
{
    const std::string home = "/home/jos\xc3\xa9/Mod Organizer \xe2\x80\x94 \xc3\xa7";
    EXPECT_EQ(filterSysPath({"/usr/lib64/python3.14", home + "/.local/lib"}, roots,
                            {home + "/plugins/plugin_python/libs"}),
              (std::vector<std::string>{"/usr/lib64/python3.14",
                                        home + "/plugins/plugin_python/libs"}));
}
