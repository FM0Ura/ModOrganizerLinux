/*
Mod Organizer archive handling - Linux support

This library is free software; you
 * can redistribute it and/or
modify it under the terms of the GNU Lesser General
 * Public
License as published by the Free Software Foundation; either
version 3 of the
 * License, or (at your option) any later version.
*/

#ifndef ARCHIVE_LINUX_WIN32_COMPAT_H
#define ARCHIVE_LINUX_WIN32_COMPAT_H

#ifdef _WIN32
#error "linux/win32_compat.h must not be used on Windows"
#endif

// On Linux, 7-Zip's own headers provide the Windows types (HRESULT, GUID, BSTR,
// PROPVARIANT, IUnknown, FILETIME, ...) and the functions of CPP/Common/MyWindows.cpp,
// which is compiled into this library (see CMakeLists.txt).
#include "Common/MyWindows.h"

#include "Common/MyCom.h"

#include <cstring>

// ATL's smart pointer, as provided by 7-Zip. Reference counts start at 0 in our
// IUnknown implementations (unknown_impl.h) so, like with ATL, constructing a CComPtr
// from a new object gives it its first reference.
template <class T>
using CComPtr = CMyComPtr<T>;

// Win32 SetFilePointer move methods, same values as the STREAM_SEEK_* ones.
#ifndef FILE_BEGIN
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#endif

// Subset of the Win32 file attributes, used for the properties reported to 7-Zip.
#ifndef FILE_ATTRIBUTE_READONLY
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_ARCHIVE 0x00000020
#define FILE_ATTRIBUTE_NORMAL 0x00000080
// 7-Zip convention: the Unix st_mode is stored in the high 16 bits, flagged by 0x8000
#define FILE_ATTRIBUTE_UNIX_EXTENSION 0x00008000
#endif

inline void PropVariantInit(PROPVARIANT* prop)
{
  std::memset(prop, 0, sizeof(PROPVARIANT));
  prop->vt = VT_EMPTY;
}

inline HRESULT PropVariantClear(PROPVARIANT* prop)
{
  return ::VariantClear(prop);
}

#endif  // ARCHIVE_LINUX_WIN32_COMPAT_H
