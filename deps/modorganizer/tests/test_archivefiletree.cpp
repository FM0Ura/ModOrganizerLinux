#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <QFile>
#include <QString>

#include <archive/archive.h>

#include "archivefiletree.h"
#include "shared/util.h"
#include "tempdir.h"

using namespace MOBase;

namespace
{

std::uint32_t crc32(const std::string& data)
{
  std::uint32_t crc = 0xffffffffu;

  for (const unsigned char c : data) {
    crc ^= c;
    for (int k = 0; k < 8; ++k) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  }

  return ~crc;
}

void put16(std::string& out, std::uint16_t v)
{
  out += static_cast<char>(v & 0xff);
  out += static_cast<char>(v >> 8);
}

void put32(std::string& out, std::uint32_t v)
{
  put16(out, static_cast<std::uint16_t>(v & 0xffff));
  put16(out, static_cast<std::uint16_t>(v >> 16));
}

// writes a ZIP file with stored (uncompressed) entries; the names are stored as
// given, backslashes included, like some Windows tools do
//
void writeZip(const std::filesystem::path& path,
              const std::vector<std::pair<std::string, std::string>>& entries)
{
  std::string out, central;

  for (const auto& [name, data] : entries) {
    const auto offset = static_cast<std::uint32_t>(out.size());
    const auto crc    = crc32(data);
    const auto size   = static_cast<std::uint32_t>(data.size());

    // local file header
    put32(out, 0x04034b50);
    put16(out, 20);      // version needed
    put16(out, 0);       // flags
    put16(out, 0);       // stored
    put16(out, 0);       // time
    put16(out, 0x0021);  // date, 1980-01-01
    put32(out, crc);
    put32(out, size);
    put32(out, size);
    put16(out, static_cast<std::uint16_t>(name.size()));
    put16(out, 0);  // extra
    out += name;
    out += data;

    // central directory header
    put32(central, 0x02014b50);
    put16(central, 20);  // made by, MS-DOS
    put16(central, 20);  // version needed
    put16(central, 0);
    put16(central, 0);
    put16(central, 0);
    put16(central, 0x0021);
    put32(central, crc);
    put32(central, size);
    put32(central, size);
    put16(central, static_cast<std::uint16_t>(name.size()));
    put16(central, 0);  // extra
    put16(central, 0);  // comment
    put16(central, 0);  // disk
    put16(central, 0);  // internal attributes
    put32(central, 0);  // external attributes
    put32(central, offset);
    central += name;
  }

  const auto centralOffset = static_cast<std::uint32_t>(out.size());
  out += central;

  // end of central directory
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<std::uint16_t>(entries.size()));
  put16(out, static_cast<std::uint16_t>(entries.size()));
  put32(out, static_cast<std::uint32_t>(central.size()));
  put32(out, centralOffset);
  put16(out, 0);

  std::ofstream f(path, std::ios::binary);
  f << out;
}

std::string slurp(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }

  return f.readAll().toStdString();
}

// files and directories under `dir` whose name contains a backslash
//
std::vector<std::string> backslashNames(const std::filesystem::path& dir)
{
  std::vector<std::string> v;

  for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
    const auto name = e.path().filename().string();
    if (name.find('\\') != std::string::npos) {
      v.push_back(e.path().string());
    }
  }

  return v;
}

// the output paths given to the archive for extraction
//
std::vector<std::wstring> outputPaths(Archive& archive)
{
  std::vector<std::wstring> v;

  for (auto* data : archive.getFileList()) {
    for (auto&& p : data->getOutputFilePaths()) {
      v.push_back(p);
    }
  }

  return v;
}

class ArchiveFileTreeTest : public testing::Test
{
protected:
  TempDir m_tmp;
  std::unique_ptr<Archive> m_archive;

  void SetUp() override
  {
    const auto zip = m_tmp.path() / "mod.zip";
    writeZip(zip, {{"textures\\armor\\a.dds", "dds"},
                   {"dir/B.txt", "world"},
                   {"readme.txt", "hello"}});

    // the 7z.so of the 7zip package, not <directory of the executable>/../lib/7z.so,
    // unless the caller already chose one
    ::setenv("MO2_7Z_LIBRARY", MO2_TESTS_7Z, 0);

    m_archive = CreateArchive();
    ASSERT_TRUE(m_archive->isValid());
    ASSERT_TRUE(m_archive->open(zip.wstring(), nullptr));
  }

  std::filesystem::path outDir(const char* name)
  {
    const auto p = m_tmp.path() / name;
    std::filesystem::create_directories(p);
    return p;
  }

  bool extract(const std::filesystem::path& dir)
  {
    return m_archive->extract(dir.wstring(), {}, {}, {});
  }
};

}  // namespace

// InstallationManager::extractFiles(): the given entries are mapped to the archive,
// extracted in a directory and their path in that directory is returned
//
TEST_F(ArchiveFileTreeTest, ExtractedEntriesAreWhereTheirPathSays)
{
  const auto tree = ArchiveFileTree::makeTree(*m_archive);
  ASSERT_TRUE(tree);

  const auto dds = tree->find("textures/armor/a.dds");
  const auto txt = tree->find("dir/B.txt");
  ASSERT_TRUE(dds);
  ASSERT_TRUE(txt);

  ArchiveFileTree::mapToArchive(*m_archive, {dds, txt});

  // with the separator of the system
  const auto paths = outputPaths(*m_archive);
  EXPECT_EQ(paths.size(), 2u);
  for (auto&& p : paths) {
    EXPECT_EQ(p.find(L'\\'), std::wstring::npos)
        << QString::fromStdWString(p).toStdString();
  }

  const auto out = outDir("extract");
  ASSERT_TRUE(extract(out));

  // the paths returned by extractFiles()
  const auto base    = QString::fromStdString(out.string());
  const auto ddsPath = base + "/" + MOShared::normalizeSeparators(dds->path());
  const auto txtPath = base + "/" + MOShared::normalizeSeparators(txt->path());

  EXPECT_TRUE(QFile::exists(ddsPath)) << ddsPath.toStdString();
  EXPECT_EQ(slurp(ddsPath), "dds");
  EXPECT_TRUE(QFile::exists(txtPath)) << txtPath.toStdString();
  EXPECT_EQ(slurp(txtPath), "world");

  // the other file is not extracted
  EXPECT_FALSE(std::filesystem::exists(out / "readme.txt"));

  EXPECT_EQ(backslashNames(out), std::vector<std::string>{});
}

// InstallationManager::doInstall(): an installer moves entries in the tree, the
// tree is mapped to the archive and extracted in the mod directory
//
TEST_F(ArchiveFileTreeTest, InstalledTreeHasDirectories)
{
  const auto tree = ArchiveFileTree::makeTree(*m_archive);
  ASSERT_TRUE(tree);

  // like an installer: dir/B.txt goes to docs/sub/B.txt
  const auto txt = tree->find("dir/B.txt");
  ASSERT_TRUE(txt);
  ASSERT_TRUE(tree->move(txt, "docs/sub/", IFileTree::InsertPolicy::FAIL_IF_EXISTS));

  tree->mapToArchive(*m_archive);

  for (auto&& p : outputPaths(*m_archive)) {
    EXPECT_EQ(p.find(L'\\'), std::wstring::npos)
        << QString::fromStdWString(p).toStdString();
  }

  const auto out = outDir("mod");
  ASSERT_TRUE(extract(out));

  EXPECT_EQ(slurp(QString::fromStdString((out / "textures/armor/a.dds").string())),
            "dds");
  EXPECT_EQ(slurp(QString::fromStdString((out / "docs/sub/B.txt").string())), "world");
  EXPECT_EQ(slurp(QString::fromStdString((out / "readme.txt").string())), "hello");

  EXPECT_EQ(backslashNames(out), std::vector<std::string>{});
}
