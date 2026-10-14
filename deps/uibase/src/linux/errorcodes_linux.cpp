// Linux implementation of errorCodeName(): error codes are errno values on Linux, the
// names are the errno macro names ("ENOENT", ...).

#include "errorcodes.h"

#include <array>
#include <cstring>
#include <string>

namespace MOBase
{

namespace
{

  // errno values are small; anything above is unknown
  constexpr std::size_t MaxErrno = 512;

  const std::array<std::wstring, MaxErrno>& errnoNames()
  {
    static const auto names = [] {
      std::array<std::wstring, MaxErrno> a;

#ifdef __GLIBC__
      for (std::size_t i = 1; i < MaxErrno; ++i) {
        if (const char* name = strerrorname_np(static_cast<int>(i))) {
          // ASCII
          a[i] = std::wstring(name, name + std::strlen(name));
        }
      }
#endif

      return a;
    }();

    return names;
  }

}  // namespace

const wchar_t* errorCodeName(DWORD code)
{
  static const wchar_t* NotFound = L"unknown error";

  if (code < MaxErrno) {
    const auto& name = errnoNames()[code];
    if (!name.empty()) {
      return name.c_str();
    }
  }

  return NotFound;
}

}  // namespace MOBase
