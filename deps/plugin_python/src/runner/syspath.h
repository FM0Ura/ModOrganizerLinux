#ifndef PYTHONRUNNER_SYSPATH_H
#define PYTHONRUNNER_SYSPATH_H

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

}  // namespace mo2::python

#endif
