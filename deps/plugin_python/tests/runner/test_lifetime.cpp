#include "gtest/gtest.h"

#include "MockOrganizer.h"
#include "TestPythonPaths.h"
#include "pythonrunner.h"

#include <QCoreApplication>

#ifndef _WIN32
// Qt's `slots` macro clashes with a member of the same name in Python's headers
#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")
#endif

TEST(Lifetime, Plugins)
{
    const auto plugins_folder = QString(std::getenv("PLUGIN_DIR"));

    auto runner = mo2::python::createPythonRunner();
    runner->initialize(testPythonPaths());

    {
        const auto objects = runner->load(plugins_folder + "/dummy-iplugin.py");

        // we found one plugin
        EXPECT_EQ(objects.size(), 1);

        // check that deleting the object actually destroys it
        bool destroyed = false;
        QObject::connect(objects[0], &QObject::destroyed, [&destroyed]() {
            destroyed = true;
        });
        delete objects[0];
        EXPECT_EQ(destroyed, true);
    }

    // same things but with a parent
    {
        QObject* dummy_parent = new QObject();
        const auto objects    = runner->load(plugins_folder + "/dummy-iplugin.py");

        // we found one plugin
        EXPECT_EQ(objects.size(), 1);
        objects[0]->setParent(dummy_parent);

        // check that deleting the object actually destroys it
        bool destroyed = false;
        QObject::connect(objects[0], &QObject::destroyed, [&destroyed]() {
            destroyed = true;
        });
        delete dummy_parent;
        EXPECT_EQ(destroyed, true);
    }
}

#ifndef _WIN32
namespace
{
    // number of live Python objects whose type is named `typeName`, found through the
    // garbage collector without collecting anything
    long liveObjectsOfType(const char* typeName)
    {
        const PyGILState_STATE state = PyGILState_Ensure();

        PyObject* globals = PyDict_New();
        PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
        PyObject* name = PyUnicode_FromString(typeName);
        PyDict_SetItemString(globals, "type_name", name);
        Py_DECREF(name);

        PyObject* result = PyRun_String(
            "import gc\n"
            "count = sum(1 for o in gc.get_objects()"
            " if type(o).__name__ == type_name)\n",
            Py_file_input, globals, globals);
        Py_XDECREF(result);

        PyObject* count = PyDict_GetItemString(globals, "count");
        const long value = count ? PyLong_AsLong(count) : -1;
        Py_DECREF(globals);

        PyGILState_Release(state);
        return value;
    }
}  // namespace

// Linux: when Mod Organizer restarts itself (instance change), the proxy, and with it
// the runner, is destroyed before the plugins, which are children of the plugin
// container; a plugin whose objects reference each other must not stay alive in
// Python after its C++ object is deleted (the garbage collector would later free it
// and touch the deleted object)
TEST(Lifetime, CyclicPluginAfterRunner)
{
    const auto plugins_folder = QString(std::getenv("PLUGIN_DIR"));

    auto runner = mo2::python::createPythonRunner();
    runner->initialize(testPythonPaths());

    QObject* parent    = new QObject();
    const auto objects = runner->load(plugins_folder + "/dummy-cyclic-iplugin.py");
    ASSERT_EQ(objects.size(), 1);
    objects[0]->setParent(parent);

    EXPECT_EQ(liveObjectsOfType("CyclicPlugin"), 1);

    bool destroyed = false;
    QObject::connect(objects[0], &QObject::destroyed, [&destroyed]() {
        destroyed = true;
    });

    runner.reset();
    delete parent;

    EXPECT_TRUE(destroyed);
    EXPECT_EQ(liveObjectsOfType("CyclicPlugin"), 0);
}
#endif
