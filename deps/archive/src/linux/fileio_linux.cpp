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

#include "../fileio.h"

#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace IO
{

namespace
{

  // Win32 FILETIME: 100ns ticks since 1601-01-01, Unix: seconds since 1970-01-01.
  constexpr Int64 kTicksPerSecond   = 10000000;
  constexpr Int64 kUnixEpochSeconds = 11644473600LL;

  FILETIME toFileTime(const struct timespec& ts)
  {
    const UInt64 ticks =
        static_cast<UInt64>((static_cast<Int64>(ts.tv_sec) + kUnixEpochSeconds) *
                            kTicksPerSecond) +
        static_cast<UInt64>(ts.tv_nsec / 100);
    FILETIME ft;
    ft.dwLowDateTime  = static_cast<DWORD>(ticks & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return ft;
  }

  struct timespec toTimespec(const FILETIME& ft)
  {
    const Int64 ticks = static_cast<Int64>(
        (static_cast<UInt64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
    Int64 secs  = ticks / kTicksPerSecond - kUnixEpochSeconds;
    Int64 nanos = (ticks % kTicksPerSecond) * 100;
    struct timespec ts;
    ts.tv_sec  = static_cast<time_t>(secs);
    ts.tv_nsec = static_cast<long>(nanos);
    return ts;
  }

}  // namespace

// The umask of the process. Reading it with umask(2) means changing it for a moment,
// which is not safe with other threads running, so read it from /proc (Linux 4.7)
// and assume the usual 022 if that is not available.
static mode_t processUmask()
{
  static const mode_t value = [] {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
      if (line.rfind("Umask:", 0) == 0) {
        try {
          return static_cast<mode_t>(std::stoul(line.substr(6), nullptr, 8) & 0777);
        } catch (...) {
          break;
        }
      }
    }
    return static_cast<mode_t>(022);
  }();
  return value;
}

// FileBase

bool FileBase::Close() noexcept
{
  if (m_Fd < 0) {
    return true;
  }
  const int fd = m_Fd;
  m_Fd         = -1;  // never retry close(2), even on EINTR/EIO
  return ::close(fd) == 0;
}

bool FileBase::GetPosition(UInt64& position) noexcept
{
  return Seek(0, FILE_CURRENT, position);
}

bool FileBase::GetLength(UInt64& length) const noexcept
{
  struct stat st;
  if (::fstat(m_Fd, &st) != 0) {
    return false;
  }
  length = static_cast<UInt64>(st.st_size);
  return true;
}

bool FileBase::Seek(Int64 distanceToMove, DWORD moveMethod,
                    UInt64& newPosition) noexcept
{
  int whence;
  switch (moveMethod) {
  case FILE_BEGIN:
    whence = SEEK_SET;
    break;
  case FILE_CURRENT:
    whence = SEEK_CUR;
    break;
  case FILE_END:
    whence = SEEK_END;
    break;
  default:
    errno = EINVAL;
    return false;
  }

  const off_t pos = ::lseek(m_Fd, static_cast<off_t>(distanceToMove), whence);
  if (pos < 0) {
    return false;
  }
  newPosition = static_cast<UInt64>(pos);
  return true;
}

bool FileBase::Seek(UInt64 position, UInt64& newPosition) noexcept
{
  return Seek(static_cast<Int64>(position), FILE_BEGIN, newPosition);
}

bool FileBase::SeekToBegin() noexcept
{
  UInt64 newPosition;
  return Seek(UInt64{0}, newPosition);
}

bool FileBase::SeekToEnd(UInt64& newPosition) noexcept
{
  return Seek(0, FILE_END, newPosition);
}

bool FileBase::Create(std::filesystem::path const& path, int flags,
                      mode_t mode) noexcept
{
  if (!Close()) {
    return false;
  }

  m_Fd = ::open(path.c_str(), flags | O_CLOEXEC, mode);
  return m_Fd >= 0;
}

bool FileBase::GetFileInformation(std::filesystem::path const& path,
                                  FileInfo* info) noexcept
{
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) {
    return false;
  }

  FileInfo result;
  result.m_Valid = true;
  try {
    result.m_Path = path;
  } catch (...) {
    errno = ENOMEM;
    return false;
  }

  UInt32 attributes = 0;
  if (S_ISDIR(st.st_mode)) {
    attributes |= FILE_ATTRIBUTE_DIRECTORY;
  } else {
    attributes |= FILE_ATTRIBUTE_ARCHIVE;
  }
  if ((st.st_mode & S_IWUSR) == 0) {
    attributes |= FILE_ATTRIBUTE_READONLY;
  }
  attributes |= FILE_ATTRIBUTE_UNIX_EXTENSION | (static_cast<UInt32>(st.st_mode) << 16);

  result.m_Attributes = attributes;
  result.m_CTime      = toFileTime(st.st_ctim);
  result.m_ATime      = toFileTime(st.st_atim);
  result.m_MTime      = toFileTime(st.st_mtim);
  result.m_Volume     = static_cast<UInt32>(st.st_dev);
  result.m_Size       = static_cast<UInt64>(st.st_size);
  result.m_Links      = static_cast<UInt32>(st.st_nlink);
  result.m_Index      = static_cast<UInt64>(st.st_ino);

  *info = std::move(result);
  return true;
}

// FileIn

bool FileIn::Open(std::filesystem::path const& filepath) noexcept
{
  return Create(filepath, O_RDONLY, 0);
}

bool FileIn::Read(void* data, UInt32 size, UInt32& processedSize) noexcept
{
  processedSize = 0;
  while (size > 0) {
    const UInt32 chunk = size > kChunkSizeMax ? kChunkSizeMax : size;
    const ssize_t n    = ::read(m_Fd, data, chunk);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      break;  // end of file
    }
    processedSize += static_cast<UInt32>(n);
    data = static_cast<unsigned char*>(data) + n;
    size -= static_cast<UInt32>(n);
  }
  return true;
}

// FileOut

bool FileOut::Open(std::filesystem::path const& fileName) noexcept
{
  return Create(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0666);
}

bool FileOut::SetTime(const FILETIME* /*cTime*/, const FILETIME* aTime,
                      const FILETIME* mTime) noexcept
{
  struct timespec times[2];
  times[0].tv_sec  = 0;
  times[0].tv_nsec = UTIME_OMIT;
  times[1].tv_sec  = 0;
  times[1].tv_nsec = UTIME_OMIT;
  if (aTime) {
    times[0] = toTimespec(*aTime);
  }
  if (mTime) {
    times[1] = toTimespec(*mTime);
  }
  return ::futimens(m_Fd, times) == 0;
}

bool FileOut::SetMTime(const FILETIME* mTime) noexcept
{
  return SetTime(nullptr, nullptr, mTime);
}

bool FileOut::Write(const void* data, UInt32 size, UInt32& processedSize) noexcept
{
  processedSize = 0;
  while (size > 0) {
    const UInt32 chunk = size > kChunkSizeMax ? kChunkSizeMax : size;
    const ssize_t n    = ::write(m_Fd, data, chunk);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (n == 0) {
      break;
    }
    processedSize += static_cast<UInt32>(n);
    data = static_cast<const unsigned char*>(data) + n;
    size -= static_cast<UInt32>(n);
  }
  return true;
}

bool FileOut::SetLength(UInt64 length) noexcept
{
  // unlike SetFilePointer + SetEndOfFile, this does not move the file position
  return ::ftruncate(m_Fd, static_cast<off_t>(length)) == 0;
}

bool FileOut::SetEndOfFile() noexcept
{
  UInt64 position;
  return GetPosition(position) && SetLength(position);
}

// attributes

bool SetFileAttributes(std::filesystem::path const& path, UInt32 attributes,
                       bool isDirectory) noexcept
{
  if ((attributes & FILE_ATTRIBUTE_UNIX_EXTENSION) == 0) {
    return true;
  }

  const mode_t stored = static_cast<mode_t>(attributes >> 16);

  // Only regular files and directories get their permissions; other types (FIFOs,
  // devices, symlinks, sockets) are extracted as plain files and keep the default mode.
  // A type of 0 is accepted: some archivers store the permission bits alone.
  const mode_t type = stored & S_IFMT;
  if (type != 0 && type != (isDirectory ? S_IFDIR : S_IFREG)) {
    return true;
  }

  // Permission bits only: never setuid, setgid or sticky. They are masked by the umask
  // like for any file created by the process, and the owner can always read and write
  // (and enter, for directories) what was extracted.
  mode_t mode = stored & 0777 & ~processUmask();
  mode |= isDirectory ? S_IRWXU : (S_IRUSR | S_IWUSR);
  return ::chmod(path.c_str(), mode) == 0;
}

}  // namespace IO
