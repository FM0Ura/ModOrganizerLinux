/*
Copyright (C) 2022 Sebastian Herbord & MO2 Team. All rights reserved.

This file is part of python proxy plugin for MO

python proxy plugin is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Python proxy plugin is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with python proxy plugin.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "proxypython.h"

#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#include <string>
#endif

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QMessageBox>
#include <QWidget>
#include <QtPlugin>

#include <uibase/log.h>
#include <uibase/utility.h>
#include <uibase/versioninfo.h>

namespace fs = std::filesystem;
using namespace MOBase;

// retrieve the path to the folder containing the proxy DLL
fs::path getPluginFolder()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    HMODULE hm = NULL;

    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)&getPluginFolder, &hm) == 0) {
        return {};
    }
    if (GetModuleFileName(hm, path, sizeof(path)) == 0) {
        return {};
    }

    return fs::path(path).parent_path();
#else
    // Linux: the folder of the proxy (plugins/), from the address of this function
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&getPluginFolder), &info) == 0 ||
        info.dli_fname == nullptr) {
        return {};
    }
    return fs::path(info.dli_fname).parent_path();
#endif
}

ProxyPython::ProxyPython()
    : m_MOInfo{nullptr}, m_RunnerLib{nullptr}, m_Runner{nullptr},
      m_LoadFailure(FailureType::NONE)
{
}

bool ProxyPython::init(IOrganizer* moInfo)
{
    m_MOInfo = moInfo;

    if (m_MOInfo && !m_MOInfo->isPluginEnabled(this)) {
        return false;
    }

#ifdef _WIN32
    if (QCoreApplication::applicationDirPath().contains(';')) {
        m_LoadFailure = FailureType::SEMICOLON;
        return true;
    }
#endif

    const auto pluginFolder = getPluginFolder();

    if (pluginFolder.empty()) {
#ifdef _WIN32
        DWORD error   = ::GetLastError();
        m_LoadFailure = FailureType::DLL_NOT_FOUND;
        log::error("failed to resolve Python proxy directory ({}): {}", error,
                   qUtf8Printable(windowsErrorString(::GetLastError())));
#else
        m_LoadFailure = FailureType::DLL_NOT_FOUND;
        log::error("failed to resolve Python proxy directory");
#endif
        return false;
    }

    if (m_MOInfo && m_MOInfo->persistent(name(), "tryInit", false).toBool()) {
        m_LoadFailure = FailureType::INITIALIZATION;
        if (QMessageBox::question(
                parentWidget(), tr("Python Initialization failed"),
                tr("On a previous start, the Python Plugin failed to initialize.\n"
                   "Do you want to try initializing Python again (at the risk of "
                   "another crash)?\n "
                   "Suggestion: Select \"no\", and click the warning sign for further "
                   "help. Afterwards you have to re-enable the Python plugin."),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) == QMessageBox::No) {
            // we force enabled here (note: this is a persistent settings since MO2 2.4
            // or something), plugin usually should not handle enabled/disabled
            // themselves but this is a base plugin so...
            m_MOInfo->setPersistent(name(), "enabled", false, true);
            return true;
        }
    }

    if (m_MOInfo) {
        m_MOInfo->setPersistent(name(), "tryInit", true);
    }

    // load the pythonrunner library, this is done in multiple steps:
    //
    // 1. we set the dlls/ subfolder (from the plugin) as the DLL directory so Windows
    // will look for DLLs in it, this is required to find the Python and libffi DLL, but
    // also the runner DLL
    //
#ifdef _WIN32
    const auto dllPaths = pluginFolder / "dlls";
    if (SetDllDirectoryW(dllPaths.c_str()) == 0) {
        DWORD error   = ::GetLastError();
        m_LoadFailure = FailureType::DLL_NOT_FOUND;
        log::error("failed to add python DLL directory ({}): {}", error,
                   qUtf8Printable(windowsErrorString(::GetLastError())));
        return false;
    }
#else
    // Linux: libpython and the runner are loaded here, at run time, not linked to the
    // proxy: a missing library (Python not installed, or another version of it) is
    // then reported as a problem of the plugin instead of failing the load of the proxy
    //
    // the handles are never closed: the interpreter cannot be finalized and started
    // again (see PythonRunner::initialize()), libpython and the runner, which hold it,
    // stay mapped until the end of the process
    const auto loadFailed = [this] {
        m_LoadFailure = FailureType::DLL_NOT_FOUND;
        if (m_MOInfo) {
            m_MOInfo->setPersistent(name(), "tryInit", false);
        }
        return true;
    };
    const auto lastDlError = [] {
        const char* error = dlerror();
        return std::string(error ? error : "unknown error");
    };

    // the extension modules of the system's Python (PyQt6, mobase) are not linked to
    // libpython and need its symbols in the global scope, but Mod Organizer loads
    // plugins with RTLD_LOCAL (spec of Phase 4d.1, section 3.1)
    if (dlopen(MO2_PYTHON_LIBRARY, RTLD_NOW | RTLD_GLOBAL) == nullptr) {
        m_PythonLibraryMissing = true;
        log::error("failed to load {}: {}", MO2_PYTHON_LIBRARY, lastDlError());
        return loadFailed();
    }

    const auto runnerPath = pluginFolder / "plugin_python" / "dlls" / "librunner.so";
    m_RunnerLib           = dlopen(runnerPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (m_RunnerLib == nullptr) {
        log::error("failed to load the Python runner {}: {}", runnerPath.string(),
                   lastDlError());
        return loadFailed();
    }

    using CreateRunner = decltype(&mo2_create_python_runner);
    const auto createRunner =
        reinterpret_cast<CreateRunner>(dlsym(m_RunnerLib, "mo2_create_python_runner"));
    if (createRunner == nullptr) {
        log::error("failed to find mo2_create_python_runner in {}: {}",
                   runnerPath.string(), lastDlError());
        return loadFailed();
    }
#endif

    // 2. we create the Python runner, we do not need to use ::LinkLibrary and
    // ::GetProcAddress because we use delayed load for the runner DLL (see the
    // CMakeLists.txt)
    //
#ifdef _WIN32
    m_Runner = mo2::python::createPythonRunner();
#else
    // Linux: the runner was loaded above, the object is deleted through the virtual
    // destructor of IPythonRunner, in the code of the runner
    m_Runner.reset(createRunner());
#endif

    if (m_Runner) {
#ifdef _WIN32
        const auto libpath = pluginFolder / "libs";
        const std::vector<fs::path> paths{
            libpath / "pythoncore.zip", libpath,
            std::filesystem::path{IOrganizer::getPluginDataPath().toStdWString()}};
#else
        // Linux: the proxy is plugins/plugin_python.so, next to the plugin_python
        // folder; the standard library is the system's (the runner filters sys.path),
        // the paths are the bytes of the file system (UTF-8); the data folder of the
        // plugins is the user's, then the one shipped with Mod Organizer (modules such
        // as DDS of preview_dds stay there)
        const std::vector<fs::path> paths{
            pluginFolder / "plugin_python" / "libs",
            fs::path{QFile::encodeName(IOrganizer::getPluginDataPath()).toStdString()},
            pluginFolder / "data"};
#endif
#ifdef _WIN32
        m_Runner->initialize(paths);
#else
        // Linux: Python may start and still fail to import mobase (e.g., without
        // PyQt6), the interpreter then stays initialized but cannot be used: the
        // failure is reported (the runner logged the error of Python) and no plugin
        // is loaded
        if (!m_Runner->initialize(paths)) {
            m_LoadFailure = FailureType::DLL_NOT_FOUND;
            m_Runner.reset();
        }
#endif
    }

    if (m_MOInfo) {
        m_MOInfo->setPersistent(name(), "tryInit", false);
    }

    // reset DLL directory
#ifdef _WIN32
    SetDllDirectoryW(NULL);
#else
    if (m_LoadFailure != FailureType::NONE) {
        return true;
    }
#endif

    if (!m_Runner || !m_Runner->isInitialized()) {
        m_LoadFailure = FailureType::INITIALIZATION;
    }
#ifdef _WIN32
    else {
        m_Runner->addDllSearchPath(pluginFolder / "dlls");
    }
#endif

    return true;
}

QString ProxyPython::name() const
{
    return "Python Proxy";
}

QString ProxyPython::localizedName() const
{
    return tr("Python Proxy");
}

QString ProxyPython::author() const
{
    return "AnyOldName3, Holt59, Silarn, Tannin";
}

QString ProxyPython::description() const
{
    return tr("Proxy Plugin to allow plugins written in python to be loaded");
}

VersionInfo ProxyPython::version() const
{
    return VersionInfo(3, 0, 0, VersionInfo::RELEASE_FINAL);
}

QList<PluginSetting> ProxyPython::settings() const
{
    return {};
}

QStringList ProxyPython::pluginList(const QDir& pluginPath) const
{
    QDir dir(pluginPath);
    dir.setFilter(dir.filter() | QDir::NoDotAndDotDot);
    QDirIterator iter(dir);

    // Note: We put python script (.py) and directory names, not the __init__.py
    // files in those since it is easier for the runner to import them.
    QStringList result;
    while (iter.hasNext()) {
        QString name   = iter.next();
        QFileInfo info = iter.fileInfo();

        if (info.isFile() && name.endsWith(".py")) {
            result.append(name);
        }
        else if (info.isDir() && QDir(info.absoluteFilePath()).exists("__init__.py")) {
            result.append(name);
        }
    }

    return result;
}

QList<QObject*> ProxyPython::load(const QString& identifier)
{
    if (!m_Runner) {
        return {};
    }
    return m_Runner->load(identifier);
}

void ProxyPython::unload(const QString& identifier)
{
    if (m_Runner) {
        return m_Runner->unload(identifier);
    }
}

std::vector<unsigned int> ProxyPython::activeProblems() const
{
    auto failure = m_LoadFailure;

    // don't know how this could happen but wth
    if (m_Runner && !m_Runner->isInitialized()) {
        failure = FailureType::INITIALIZATION;
    }

    if (failure != FailureType::NONE) {
        return {static_cast<std::underlying_type_t<FailureType>>(failure)};
    }

    return {};
}

QString ProxyPython::shortDescription(unsigned int key) const
{
    switch (static_cast<FailureType>(key)) {
    case FailureType::SEMICOLON:
        return tr("ModOrganizer path contains a semicolon");
    case FailureType::DLL_NOT_FOUND:
#ifdef _WIN32
        return tr("Python DLL not found");
#else
        return tr("Python could not be loaded");
#endif
    case FailureType::INVALID_DLL:
        return tr("Invalid Python DLL");
    case FailureType::INITIALIZATION:
        return tr("Initializing Python failed");
    default:
        return tr("invalid problem key %1").arg(key);
    }
}

QString ProxyPython::fullDescription(unsigned int key) const
{
    switch (static_cast<FailureType>(key)) {
    case FailureType::SEMICOLON:
        return tr("The path to Mod Organizer (%1) contains a semicolon.<br>"
                  "While this is legal on NTFS drives, many applications do not "
                  "handle it correctly.<br>"
                  "Unfortunately MO depends on libraries that seem to fall into that "
                  "group.<br>"
                  "As a result the python plugin cannot be loaded, and the only "
                  "solution we can offer is to remove the semicolon or move MO to a "
                  "path without a semicolon.")
            .arg(QCoreApplication::applicationDirPath());
    case FailureType::DLL_NOT_FOUND:
#ifdef _WIN32
        return tr("The Python plugin DLL was not found, maybe your antivirus deleted "
                  "it. Re-installing MO2 might fix the problem.");
#else
    {
        QString text =
            tr("Python %1 or its modules (PyQt6) could not be loaded, the log of "
               "Mod Organizer has the error. In the mo2-run distrobox, install them "
               "with: sudo dnf install python3 python3-pyqt6")
                .arg(QStringLiteral(MO2_PYTHON_VERSION_STRING));
        if (m_PythonLibraryMissing) {
            text += "<br>" + tr("This build of Mod Organizer expects Python %1 (%2): "
                                "for another version of Python, Mod Organizer must be "
                                "rebuilt.")
                                 .arg(QStringLiteral(MO2_PYTHON_VERSION_STRING))
                                 .arg(QStringLiteral(MO2_PYTHON_LIBRARY));
        }
        return text;
    }
#endif
    case FailureType::INVALID_DLL:
        return tr(
            "The Python plugin DLL is invalid, maybe your antivirus is blocking it. "
            "Re-installing MO2 and adding exclusions for it to your AV might fix the "
            "problem.");
    case FailureType::INITIALIZATION:
        return tr("The initialization of the Python plugin DLL failed, unfortunately "
                  "without any details.");
    default:
        return tr("invalid problem key %1").arg(key);
    }
}

bool ProxyPython::hasGuidedFix(unsigned int) const
{
    return false;
}

void ProxyPython::startGuidedFix(unsigned int) const {}
