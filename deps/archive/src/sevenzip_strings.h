/*
Mod Organizer archive handling

This library is free software; you can redistribute
 * it and/or
modify it under the terms of the GNU Lesser General Public
License as
 * published by the Free Software Foundation; either
version 3 of the License, or (at
 * your option) any later version.
*/

#ifndef ARCHIVE_SEVENZIP_STRINGS_H
#define ARCHIVE_SEVENZIP_STRINGS_H

#include <string>
#include <string_view>

#ifndef _WIN32
#include "linux/utf.h"
#endif

namespace ArchiveStrings
{

// On Windows wchar_t is UTF-16 and nothing needs converting. On Linux wchar_t is UTF-32
// but 7-Zip stores non-BMP characters as surrogate pairs in it.

/**
 * @brief Convert a string received from 7-Zip (path, name, ...) to the strings
 * this
 *     library exposes.
 */
inline std::wstring fromSevenZip(std::wstring_view s)
{
#ifdef _WIN32
  return std::wstring(s);
#else
  return ArchiveUtf::combineSurrogates(s);
#endif
}

/**
 * @brief Convert a string given by this library's users (e.g. a password) to the
 * form
 *     7-Zip expects.
 */
inline std::wstring toSevenZip(std::wstring_view s)
{
#ifdef _WIN32
  return std::wstring(s);
#else
  return ArchiveUtf::splitToSurrogates(s);
#endif
}

}  // namespace ArchiveStrings

#endif  // ARCHIVE_SEVENZIP_STRINGS_H
