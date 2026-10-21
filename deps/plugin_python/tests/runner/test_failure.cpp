#include <gtest/gtest.h>

#include <pybind11/embed.h>

#include <QTemporaryDir>

#include <fstream>

#include <uibase/log.h>

#include "pythonrunner.h"

// a Python that starts but cannot import mobase (e.g., PyQt6 is not installed) is a
// failure of initialize(), which neither throws nor keeps the GIL (Linux, Phase 4d.1)
TEST(Failure, AMobaseThatCannotBeImportedIsAFailure)
{
    // the runner logs the error of Python
    MOBase::log::LoggerConfiguration conf;
    conf.name     = "runner-failure-tests";
    conf.maxLevel = MOBase::log::Debug;
    conf.pattern  = "%v";
    MOBase::log::createDefault(conf);

    // the only mobase on the paths of the runner (the system's site-packages has none)
    // fails like the real one without PyQt6
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = dir.path().toStdString();
    std::filesystem::create_directories(root / "mobase");
    std::ofstream(root / "mobase" / "__init__.py")
        << "raise ImportError(\"No module named 'PyQt6'\")\n";

    auto runner      = mo2::python::createPythonRunner();
    bool initialized = true;
    EXPECT_NO_THROW(initialized = runner->initialize({root}));
    EXPECT_FALSE(initialized);

    // the interpreter cannot be finalized, but the GIL is released so that another
    // thread does not deadlock on it
    ASSERT_TRUE(Py_IsInitialized());
    EXPECT_EQ(PyGILState_Check(), 0);
}
