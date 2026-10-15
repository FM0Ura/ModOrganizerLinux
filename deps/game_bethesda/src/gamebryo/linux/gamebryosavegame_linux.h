// Linux additions to GamebryoSaveGame::FileWrapper, included at the end of
// gamebryosavegame.h on Linux only.
//
// The save formats store the 32-bit long of Windows (LLP64): the readers spell those
// fields `long` / `unsigned long`, which are 64-bit on Linux (LP64), so reading and
// skipping them is specialized to 4 bytes here. 64-bit fields are spelled
// `unsigned long long` (the type of std::uint64_t with MSVC): on Linux std::uint64_t is
// `unsigned long`, the same type as the 32-bit fields, so it must not be used for
// on-disk fields read through read<T>() / skip<T>().

#ifndef GAMEBRYOSAVEGAME_LINUX_H
#define GAMEBRYOSAVEGAME_LINUX_H

#include <cstdint>

static_assert(sizeof(unsigned long long) == 8);

// GCC has no explicit specializations in class scope: the one of the class for
// QString is declared here on Linux
template <>
void GamebryoSaveGame::FileWrapper::read<QString>(QString& value);

template <>
inline void GamebryoSaveGame::FileWrapper::read<unsigned long>(unsigned long& value)
{
  std::uint32_t v;
  read(v);
  value = v;
}

template <>
inline void GamebryoSaveGame::FileWrapper::read<long>(long& value)
{
  std::int32_t v;
  read(v);
  value = v;
}

template <>
inline void GamebryoSaveGame::FileWrapper::skip<unsigned long>(int count)
{
  skip<std::uint32_t>(count);
}

template <>
inline void GamebryoSaveGame::FileWrapper::skip<long>(int count)
{
  skip<std::int32_t>(count);
}

#endif  // GAMEBRYOSAVEGAME_LINUX_H
