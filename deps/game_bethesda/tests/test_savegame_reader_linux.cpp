// GamebryoSaveGame::FileWrapper on Linux: the 32-bit long of the save formats (spelled
// long / unsigned long, 64-bit on Linux) is read and skipped as 4 bytes, 64-bit fields
// (unsigned long long, readLong()) as 8 bytes.

#include <gtest/gtest.h>

#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>

#include "gamebryosavegame.h"

#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace
{

// FileWrapper is a protected member type of GamebryoSaveGame
struct SaveAccess : GamebryoSaveGame
{
  using GamebryoSaveGame::FileWrapper;
};
using FileWrapper = SaveAccess::FileWrapper;

}  // namespace

TEST(SaveGameReader, ReadsWindowsLongAnd64BitFields)
{
  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString path = tmp.filePath("test.sav");

  {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);

    s.writeRawData("TEST", 4);
    s << quint32(0x11223344);          // unsigned long
    s << qint32(-5);                   // long
    s << quint32(0xdeadbeef);          // skip<unsigned long>
    s << quint64(0x0102030405060708);  // readLong()
    s << quint64(0xffffffffffffffff);  // skip<unsigned long long> (Starfield header)
    s << quint64(0x1122334455667788);  // unsigned long long (chunk offsets)
    s << quint32(0xcafef00d);          // readInt()
  }

  FileWrapper file(path, "TEST");

  unsigned long ul = 0;
  file.read(ul);
  EXPECT_EQ(ul, 0x11223344ul);

  long l = 0;
  file.read(l);
  EXPECT_EQ(l, -5);

  file.skip<unsigned long>();

  EXPECT_EQ(file.readLong(), 0x0102030405060708ull);

  file.skip<unsigned long long>();

  unsigned long long ull = 0;
  file.read(ull);
  EXPECT_EQ(ull, 0x1122334455667788ull);

  EXPECT_EQ(file.readInt(), 0xcafef00du);

  // nothing left
  unsigned char c;
  EXPECT_THROW(file.read(c), std::runtime_error);
}

// Why scripts/check-save-reads.sh exists: on Linux std::uint64_t is `unsigned long`,
// the type specialised to the 4-byte Windows long, so read/skip<std::uint64_t> consume
// 4 bytes. This pins that behaviour; 64-bit fields use unsigned long long or
// readLong().
TEST(SaveGameReader, StdUint64IsTheWindowsLongOnLinux)
{
  static_assert(std::is_same_v<std::uint64_t, unsigned long>);

  QTemporaryDir tmp;
  ASSERT_TRUE(tmp.isValid());
  const QString path = tmp.filePath("test.sav");
  {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    QDataStream s(&file);
    s.setByteOrder(QDataStream::LittleEndian);
    s.writeRawData("TEST", 4);
    s << quint64(0x1122334455667788);
  }

  FileWrapper file(path, "TEST");

  std::uint64_t v = 0;
  file.read(v);  // save-read-ok: pins the 4-byte behaviour
  EXPECT_EQ(v, 0x55667788ull);

  // the upper half of the field is still unread
  EXPECT_EQ(file.readInt(), 0x11223344u);
}
