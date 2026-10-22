#include "syspath.h"

#include <algorithm>

#include <pybind11/embed.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace mo2::python {

    namespace {
        bool isUnder(const std::string& path, std::string root)
        {
            while (root.size() > 1 && root.back() == '/') {
                root.pop_back();
            }
            return path == root || (path.size() > root.size() &&
                                    path.compare(0, root.size(), root) == 0 &&
                                    path[root.size()] == '/');
        }
    }  // namespace

    std::vector<std::string> filterSysPath(const std::vector<std::string>& sysPath,
                                           const std::vector<std::string>& allowedRoots,
                                           const std::vector<std::string>& extraPaths)
    {
        std::vector<std::string> result;
        for (const auto& path : sysPath) {
            if (std::any_of(allowedRoots.begin(), allowedRoots.end(),
                            [&](const auto& root) {
                                return isUnder(path, root);
                            })) {
                result.push_back(path);
            }
        }
        for (const auto& path : extraPaths) {
            if (std::find(result.begin(), result.end(), path) == result.end()) {
                result.push_back(path);
            }
        }
        return result;
    }

    std::vector<std::string> systemSysPathRoots()
    {
        py::module_ sys       = py::module_::import("sys");
        py::module_ sysconfig = py::module_::import("sysconfig");

        const auto base = sys.attr("base_prefix").cast<std::string>();
        const auto version =
            "python" + sysconfig.attr("get_python_version")().cast<std::string>();

        // the paths of the standard library from sys.base_prefix: the default
        // variables of the sysconfig of Fedora point to /usr/local outside of RPM
        // builds (platstdlib would be /usr/local/lib64/python3.X)
        py::dict vars;
        vars["base"]               = base;
        vars["platbase"]           = base;
        vars["installed_base"]     = base;
        vars["installed_platbase"] = base;

        return {sysconfig.attr("get_path")("stdlib", "posix_prefix", vars)
                    .cast<std::string>(),
                sysconfig.attr("get_path")("platstdlib", "posix_prefix", vars)
                    .cast<std::string>(),
                base + "/lib64/" + version + "/site-packages",
                base + "/lib/" + version + "/site-packages"};
    }

}  // namespace mo2::python
