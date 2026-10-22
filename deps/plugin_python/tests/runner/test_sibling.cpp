#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <fstream>

#include <uibase/iplugin.h>
#include <uibase/log.h>

#include "TestPythonPaths.h"
#include "pythonrunner.h"

// a single-file plugin may import a sibling module of its folder (FNISPatches.py imports
// FNISTool.py), whatever the order in which the filesystem lists the plugins (Linux,
// Phase 4d.1)
TEST(Sibling, ASingleFilePluginImportsItsSiblingModule)
{
    MOBase::log::LoggerConfiguration conf;
    conf.name     = "runner-sibling-tests";
    conf.maxLevel = MOBase::log::Debug;
    conf.pattern  = "%v";
    MOBase::log::createDefault(conf);

    // the folder of the plugin is not on the paths of the runner
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = dir.path().toStdString();
    std::ofstream(root / "sibling_helper.py") << "VALUE = 42\n";
    std::ofstream(root / "plugin_with_sibling.py")
        << "import mobase\n"
           "from sibling_helper import VALUE\n"
           "\n"
           "class SiblingPlugin(mobase.IPlugin):\n"
           "    def init(self, organizer):\n"
           "        return True\n"
           "    def author(self):\n"
           "        return \"The Author\"\n"
           "    def name(self):\n"
           "        return f\"sibling-{VALUE}\"\n"
           "    def description(self):\n"
           "        return \"The Description\"\n"
           "    def version(self):\n"
           "        return mobase.VersionInfo(1, 0, 0)\n"
           "    def settings(self):\n"
           "        return []\n"
           "\n"
           "def createPlugin():\n"
           "    return SiblingPlugin()\n";

    auto runner = mo2::python::createPythonRunner();
    ASSERT_TRUE(runner->initialize(testPythonPaths()));

    const auto objects = runner->load(
        QString::fromStdString((root / "plugin_with_sibling.py").generic_string()));
    ASSERT_EQ(objects.size(), 1);

    const MOBase::IPlugin* plugin = qobject_cast<MOBase::IPlugin*>(objects[0]);
    ASSERT_NE(plugin, nullptr);
    EXPECT_EQ(plugin->name(), "sibling-42");
}
