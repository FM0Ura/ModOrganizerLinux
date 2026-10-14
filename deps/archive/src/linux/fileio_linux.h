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

#ifndef ARCHIVE_LINUX_FILEIO_H
#define ARCHIVE_LINUX_FILEIO_H

// POSIX implementation of the IO::File* classes of fileio.h (included from there).

#include "win32_compat.h"

#include "7zip/Archive/IArchive.h"

#include <sys/types.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "utf.h"

namespace IO
{

/**
 * Information about a file, in the units 7-Zip expects (FILETIME, Win32-like
 *
 * attributes), filled from stat().
 */
class FileInfo
{
public:
  FileInfo() : m_Valid{false} {}

  bool isValid() const { return m_Valid; }

  const std::filesystem::path& path() const { return m_Path; }

  UInt32 fileAttributes() const { return m_Attributes; }
  FILETIME creationTime() const { return m_CTime; }
  FILETIME lastAccessTime() const { return m_ATime; }
  FILETIME lastWriteTime() const { return m_MTime; }
  UInt32 volumeSerialNumber() const { return m_Volume; }
  UInt64 fileSize() const { return m_Size; }
  UInt32 numberOfLinks() const { return m_Links; }
  UInt64 fileInfex() const { return m_Index; }

  bool isDir() const { return (m_Attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
  bool isReadOnly() const { return (m_Attributes & FILE_ATTRIBUTE_READONLY) != 0; }

private:
  friend class FileBase;

  bool m_Valid;
  std::filesystem::path m_Path;
  UInt32 m_Attributes = 0;
  FILETIME m_CTime{};
  FILETIME m_ATime{};
  FILETIME m_MTime{};
  UInt32 m_Volume = 0;
  UInt64 m_Size   = 0;
  UInt32 m_Links  = 0;
  UInt64 m_Index  = 0;
};

class FileBase
{
public:  // Constructors, destructor, assignment.
  FileBase() noexcept : m_Fd{-1} {}

  FileBase(FileBase&& other) noexcept : m_Fd{other.m_Fd} { other.m_Fd = -1; }

  ~FileBase() noexcept { Close(); }

  FileBase(FileBase const&)            = delete;
  FileBase& operator=(FileBase const&) = delete;
  FileBase& operator=(FileBase&&)      = delete;

public:  // Operations
  bool Close() noexcept;

  bool GetPosition(UInt64& position) noexcept;
  bool GetLength(UInt64& length) const noexcept;

  // moveMethod is FILE_BEGIN, FILE_CURRENT or FILE_END (same values as SEEK_*)
  bool Seek(Int64 distanceToMove, DWORD moveMethod, UInt64& newPosition) noexcept;
  bool Seek(UInt64 position, UInt64& newPosition) noexcept;
  bool SeekToBegin() noexcept;
  bool SeekToEnd(UInt64& newPosition) noexcept;

  static bool GetFileInformation(std::filesystem::path const& path,
                                 FileInfo* info) noexcept;

protected:
  // flags and mode as for open(2)
  bool Create(std::filesystem::path const& path, int flags, mode_t mode) noexcept;

protected:
  static constexpr UInt32 kChunkSizeMax = (1 << 22);

  int m_Fd;
};

class FileIn : public FileBase
{
public:
  using FileBase::FileBase;

public:  // Operations
  bool Open(std::filesystem::path const& filepath) noexcept;

  // Reads until size bytes are read or the end of the file is reached.
  bool Read(void* data, UInt32 size, UInt32& processedSize) noexcept;
};

class FileOut : public FileBase
{
public:
  using FileBase::FileBase;

public:  // Operations:
  // Creates the file, or truncates it if it exists.
  bool Open(std::filesystem::path const& fileName) noexcept;

  // Null pointers leave the corresponding time unchanged; the creation time cannot be
  // set on Linux and is ignored.
  bool SetTime(const FILETIME* cTime, const FILETIME* aTime,
               const FILETIME* mTime) noexcept;
  bool SetMTime(const FILETIME* mTime) noexcept;
  bool Write(const void* data, UInt32 size, UInt32& processedSize) noexcept;

  bool SetLength(UInt64 length) noexcept;
  bool SetEndOfFile() noexcept;
};

/**
 * @brief Convert a wchar_t (UTF-32) string to a path, i.e. to UTF-8 bytes.
 *
 *
 * Archives often come from Windows, so backslashes are treated as separators.
 */
inline std::filesystem::path to_path(std::wstring_view str)
{
  std::string utf8 = ArchiveUtf::toUtf8(str);
  std::replace(utf8.begin(), utf8.end(), '\\', '/');
  return std::filesystem::path(std::move(utf8));
}

/**
 * @brief Convert a path (UTF-8 bytes) to a wchar_t (UTF-32) string.
 */
inline std::wstring to_wstring(std::filesystem::path const& path)
{
  return ArchiveUtf::fromUtf8(path.native());
}

/**
 * @brief Convert the given wide-string to an absolute, normalized path.
 *
 * There
 * is no long-path prefix on Linux.
 */
inline std::filesystem::path make_path(std::wstring const& pathstr)
{
  std::filesystem::path path = to_path(pathstr);
  if (!path.is_absolute()) {
    path = std::filesystem::absolute(path);
  }
  return path.lexically_normal();
}

/**
 * @brief Apply the attributes 7-Zip reports for an entry to an extracted file.
 *

 * * When the archive stores Unix permissions (7-Zip convention: 0x8000 flag and
 * st_mode
 * in the high 16 bits) they are applied, always keeping the owner able to
 * read, write
 * (and enter, for directories) the result. Windows attributes are
 * ignored: a read-only
 * flag would only make the extracted mod files harder to
 * replace.
 *
 * @return true if nothing had to be changed or the change succeeded.
 */
bool SetFileAttributes(std::filesystem::path const& path, UInt32 attributes,
                       bool isDirectory) noexcept;

}  // namespace IO

#endif  // ARCHIVE_LINUX_FILEIO_H
