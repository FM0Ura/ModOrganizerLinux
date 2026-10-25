#ifndef PYTHONRUNNER_SYSPATH_H
#define PYTHONRUNNER_SYSPATH_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "pythonrunner.h"

namespace mo2::python {

    // the entries of sys.path under one of the allowed roots, in their order, then the
    // extra paths (the folders of Mod Organizer) not already there; a root matches
    // itself and what is under it (Linux: the Python of Mod Organizer only sees the
    // system's standard library and packages, never the user's, spec of Phase 4d.1)
    //
    // the paths are the bytes of the file system (UTF-8 on Linux), compared as they are
    //
    RUNNER_DLL_EXPORT std::vector<std::string>
    filterSysPath(const std::vector<std::string>& sysPath,
                  const std::vector<std::string>& allowedRoots,
                  const std::vector<std::string>& extraPaths);

    // the allowed roots of sys.path for the running Python (Linux): its standard
    // library and the site-packages of the system's packages under sys.base_prefix,
    // never under /usr/local (which the sysconfig of Fedora gives outside of RPM
    // builds); Python must be initialized and the GIL held
    //
    RUNNER_DLL_EXPORT std::vector<std::string> systemSysPathRoots();

    // the home of the Python bundled with Mod Organizer (Linux AppImage): the folder
    // two levels above the plugin folder (usr, with bin/plugins and lib/python3.X) if
    // it holds the standard library of the running version (lib/python3.X/os.py),
    // nullopt in a development tree or an installation using the system's Python
    //
    RUNNER_DLL_EXPORT std::optional<std::filesystem::path>
    bundledPythonHome(const std::filesystem::path& pluginFolder);

}  // namespace mo2::python

#endif
