#ifndef UIBASE_ERRORCODES_H
#define UIBASE_ERRORCODES_H

#include "dllimport.h"
#ifdef _WIN32
#include <Windows.h>
#else
#include "win32compat.h"
#endif

namespace MOBase
{

QDLLEXPORT const wchar_t* errorCodeName(DWORD code);

}  // namespace MOBase

#endif  // UIBASE_ERRORCODES_H
