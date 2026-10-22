#include <gtest/gtest.h>

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <QTemporaryDir>

#include <cstdlib>
#include <fstream>

#include "TestPythonPaths.h"
#include "pythonrunner.h"
#include "syspath.h"

namespace py = pybind11;

namespace {
    void makePackage(const std::filesystem::path& dir, const std::string& name)
    {
        std::filesystem::create_directories(dir / name);
        std::ofstream(dir / name / "__init__.py") << "VALUE = 1\n";
    }
}  // namespace

// a package of the user (~/.local) or of PYTHONPATH is never visible to the plugins
// (Linux, spec of Phase 4d.1, section 5)
TEST(Isolation, UserPackagesAreInvisible)
{
    QTemporaryDir home;
    ASSERT_TRUE(home.isValid());
    const std::filesystem::path root = home.path().toStdString();
    makePackage(root / (".local/lib/python" MO2_PYTHON_VERSION_STRING "/site-packages"),
                "mo2_leak_user");
    makePackage(root / "pythonpath", "mo2_leak_env");

    // the paths of the test (mobase) are read before PYTHONPATH changes
    const auto paths = testPythonPaths();
    setenv("HOME", root.c_str(), 1);
    setenv("PYTHONPATH", (root / "pythonpath").c_str(), 1);

    auto runner = mo2::python::createPythonRunner();
    ASSERT_TRUE(runner->initialize(paths));

    py::gil_scoped_acquire gil;
    py::module_ util = py::module_::import("importlib.util");
    EXPECT_TRUE(util.attr("find_spec")("mo2_leak_user").is_none());
    EXPECT_TRUE(util.attr("find_spec")("mo2_leak_env").is_none());

    // the site module would have added the user's site-packages, had it been allowed
    py::module_ site = py::module_::import("site");
    EXPECT_EQ(
        site.attr("getusersitepackages")().cast<std::string>().rfind(root.string(), 0),
        0u);

    // the allowed roots are those of the system's Python, never /usr/local
    const auto base =
        py::module_::import("sys").attr("base_prefix").cast<std::string>();
    for (const auto& root : mo2::python::systemSysPathRoots()) {
        EXPECT_EQ(root.rfind(base + "/lib", 0), 0u) << root;
    }

    for (const auto& entry :
         py::module_::import("sys").attr("path").cast<std::vector<std::string>>()) {
        EXPECT_EQ(entry.find(root.string()), std::string::npos) << entry;
        EXPECT_NE(entry.rfind("/usr/local", 0), 0u) << entry;
    }
    EXPECT_FALSE(util.attr("find_spec")("mobase").is_none());
}
