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

#ifndef ARCHIVE_LINUX_UTF_H
#define ARCHIVE_LINUX_UTF_H

// On Linux wchar_t is UTF-32 while file names are bytes (UTF-8 by convention). The
// locale-dependent conversions of the C library (wcstombs, std::filesystem::path from
// a wide string) fail on non-ASCII names with the default "C" locale, so convert
// explicitly.

#include <string>
#include <string_view>

namespace ArchiveUtf
{

inline void appendUtf8(std::string& out, char32_t cp)
{
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    cp = 0xFFFD;
  }
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

inline bool isHighSurrogate(char32_t c)
{
  return c >= 0xD800 && c <= 0xDBFF;
}

inline bool isLowSurrogate(char32_t c)
{
  return c >= 0xDC00 && c <= 0xDFFF;
}

// 7-Zip keeps its strings as UTF-16 code units even where wchar_t is 32-bit: a
// character outside the BMP is a high+low surrogate pair of two wchar_t. These
// functions convert between that and strings holding real code points.

// UTF-16 code units in wchar_t (from 7-Zip) -> code points. Unpaired surrogates become
// U+FFFD.
inline std::wstring combineSurrogates(std::wstring_view s)
{
  std::wstring out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char32_t c = static_cast<char32_t>(s[i]);
    if (isHighSurrogate(c) && i + 1 < s.size() &&
        isLowSurrogate(static_cast<char32_t>(s[i + 1]))) {
      const char32_t low = static_cast<char32_t>(s[i + 1]);
      out.push_back(
          static_cast<wchar_t>(0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00)));
      ++i;
    } else if (isHighSurrogate(c) || isLowSurrogate(c)) {
      out.push_back(0xFFFD);
    } else {
      out.push_back(static_cast<wchar_t>(c));
    }
  }
  return out;
}

// code points -> UTF-16 code units in wchar_t (for 7-Zip).
inline std::wstring splitToSurrogates(std::wstring_view s)
{
  std::wstring out;
  out.reserve(s.size());
  for (wchar_t w : s) {
    const char32_t c = static_cast<char32_t>(w);
    if (c >= 0x10000 && c <= 0x10FFFF) {
      out.push_back(static_cast<wchar_t>(0xD800 + ((c - 0x10000) >> 10)));
      out.push_back(static_cast<wchar_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
    } else {
      out.push_back(w);
    }
  }
  return out;
}

// wchar_t string (UTF-32) -> UTF-8. Invalid code points become U+FFFD. Surrogate pairs
// (UTF-16 code units, see above) are combined rather than rejected.
inline std::string toUtf8(std::wstring_view s)
{
  std::string out;
  const std::wstring combined = combineSurrogates(s);
  out.reserve(combined.size());
  for (wchar_t c : combined) {
    appendUtf8(out, static_cast<char32_t>(c));
  }
  return out;
}

// UTF-8 -> wchar_t string (UTF-32). Invalid sequences become U+FFFD, one per byte.
inline std::wstring fromUtf8(std::string_view s)
{
  std::wstring out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int extra;
    char32_t cp, min;
    if (c < 0x80) {
      out.push_back(static_cast<wchar_t>(c));
      ++i;
      continue;
    } else if (c >= 0xC2 && c < 0xE0) {
      extra = 1;
      cp    = c & 0x1F;
      min   = 0x80;
    } else if (c >= 0xE0 && c < 0xF0) {
      extra = 2;
      cp    = c & 0x0F;
      min   = 0x800;
    } else if (c >= 0xF0 && c < 0xF5) {
      extra = 3;
      cp    = c & 0x07;
      min   = 0x10000;
    } else {
      out.push_back(0xFFFD);
      ++i;
      continue;
    }

    // the whole sequence must be present
    bool ok = (i + static_cast<std::size_t>(extra)) < s.size();
    for (int k = 1; ok && k <= extra; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) {
        ok = false;
      } else {
        cp = (cp << 6) | (cc & 0x3F);
      }
    }
    if (ok && cp >= min && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
      out.push_back(static_cast<wchar_t>(cp));
      i += 1 + extra;
    } else {
      out.push_back(0xFFFD);
      ++i;
    }
  }
  return out;
}

}  // namespace ArchiveUtf

#endif  // ARCHIVE_LINUX_UTF_H
