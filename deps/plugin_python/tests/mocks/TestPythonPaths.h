#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// the module paths of the tests (mobase, the test plugins), from PYTHONPATH as the test
// environment sets it; the runner itself never reads PYTHONPATH on Linux
inline std::vector<std::filesystem::path> testPythonPaths()
{
    std::vector<std::filesystem::path> paths;
    const char* env = std::getenv("PYTHONPATH");
    if (env == nullptr) {
        return paths;
    }
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::string value(env);
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end  = value.find(separator, start);
        const auto item = value.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (!item.empty()) {
            paths.emplace_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return paths;
}
