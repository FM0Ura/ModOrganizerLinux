// Tests of the Linux bs_archive_auto (deps/bsapacker/linux, spec of Phase 4e, sections
// 3.2 and 5.3): archives written through the libbsarch-compatible API read back with
// libbsarchpp and, where it reads the format, with bsatk.
#include <libbsarch/bs_archive_auto.hpp>
#include <libbsarchpp/Bsa.h>

#include <bsatk/bsatk.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;

namespace
{

class TempDir
{
public:
  TempDir()
  {
    std::string tmpl = (fs::temp_directory_path() / "bsapacker-tests-XXXXXX").string();
    if (mkdtemp(tmpl.data()) == nullptr) {
      throw std::runtime_error("mkdtemp failed");
    }
    m_path = tmpl;
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::permissions(m_path, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(m_path, ec);
  }
  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

void writeFile(const fs::path& path, const std::string& content)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << content;
}

std::string readFile(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

// lower case, '/' separators: archives store names in their own case and separator
std::string normalized(std::string path)
{
  std::replace(path.begin(), path.end(), '\\', '/');
  std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return path;
}

// relative path -> content of the files of a mod folder
std::map<std::string, std::string> looseFiles(const fs::path& root)
{
  std::map<std::string, std::string> files;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) {
      files[normalized(fs::relative(entry.path(), root).generic_string())] =
          readFile(entry.path());
    }
  }
  return files;
}

// a mod folder whose path has spaces, with mixed-case folders and a compressible file
fs::path makeMod(const fs::path& base)
{
  const fs::path root = base / "mods" / "My Test Mod";
  writeFile(root / "Meshes" / "Armor" / "Cuirass.nif", std::string(8192, 'n'));
  writeFile(root / "scripts" / "test.pex", "pex data");
  return root;
}

// packs every file of root with the libbsarch-compatible API, as the builders do
void pack(const fs::path& root, bsa_archive_type_t type, bool compressed,
          const fs::path& archive)
{
  libbsarch::bs_archive_auto bsa(type);
  const std::wstring rootString = (root.wstring() + L'/');
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) {
      bsa.add_file_from_disk(libbsarch::disk_blob(rootString, entry.path().wstring()));
    }
  }
  bsa.set_compressed(compressed);
  bsa.set_dds_callback(nullptr, rootString);
  bsa.save_to_disk(archive);
}

// relative path -> content, as libbsarchpp reads the archive back
std::map<std::string, std::string> readWithLibbsarchpp(const fs::path& archive)
{
  // multithreaded: libbsarchpp 1.0.7 unlocks a mutex it holds only in that mode when it
  // extracts a compressed FO4/SF file, and throws EPERM otherwise (Bsa.cpp, extractFileData)
  libbsarchpp::Bsa bsa(archive, true);
  std::map<std::string, std::string> files;
  for (const auto& name : bsa.getFileList()) {
    const auto data = bsa.extractFileData(name);
    files[normalized(name.generic_string())] = std::string(data.begin(), data.end());
  }
  return files;
}

// a DXT1 texture of 64x64 pixels, one mip level: a legacy DDS header (128 bytes) and
// 2048 bytes of blocks
std::string makeDds()
{
  std::string dds(128, '\0');
  auto put = [&dds](std::size_t offset, uint32_t value) {
    std::memcpy(dds.data() + offset, &value, sizeof(value));
  };
  std::memcpy(dds.data(), "DDS ", 4);
  put(4, 124);                                // header size
  put(8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000);  // CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
  put(12, 64);                                // height
  put(16, 64);                                // width
  put(20, 2048);                              // linear size
  put(28, 1);                                 // mip levels
  put(76, 32);                                // pixel format size
  put(80, 0x4);                               // DDPF_FOURCC
  std::memcpy(dds.data() + 84, "DXT1", 4);
  put(108, 0x1000);                           // DDSCAPS_TEXTURE
  for (int i = 0; i < 2048; ++i) {
    dds.push_back(static_cast<char>(i * 7));
  }
  return dds;
}

struct Format
{
  const char* name;
  bsa_archive_type_t type;
  const char* extension;
  bool textures;     // FO4dds/SFdds: only DDS files, header rebuilt by the archive
  bool bsatkReads;   // bsatk is an independent reader of this format (spec §5.3)
  bool compressible;
};

std::string formatName(const testing::TestParamInfo<Format>& info)
{
  return info.param.name + std::string(info.param.compressible ? "" : "_Stored");
}

// relative path -> content, as bsatk extracts the archive
std::map<std::string, std::string> readWithBsatk(const fs::path& archive, const fs::path& out)
{
  // bsatk creates the folders of the archive, but not the output folder itself
  fs::create_directories(out);
  BSA::Archive bsa;
  const auto read = bsa.read(archive, true);
  if (read != BSA::ERROR_NONE) {
    throw std::runtime_error("bsatk could not read the archive: " + std::to_string(read));
  }
  if (bsa.extractAll(out, [](int, std::string) { return true; }) != BSA::ERROR_NONE) {
    throw std::runtime_error("bsatk could not extract the archive");
  }
  return looseFiles(out);
}

}  // namespace

class BsArchiveAutoFormat : public testing::TestWithParam<Format>
{};

TEST_P(BsArchiveAutoFormat, RoundTrip)
{
  const Format& format = GetParam();
  TempDir dir;
  const fs::path root = dir.path() / "mods" / "My Test Mod";
  if (format.textures) {
    writeFile(root / "Textures" / "Armor" / "Iron" / "Cuirass.dds", makeDds());
    writeFile(root / "textures" / "sky.dds", makeDds());
  } else {
    writeFile(root / "Meshes" / "Armor" / "Iron" / "Cuirass.nif", std::string(8192, 'n'));
    writeFile(root / "Textures" / "Armor" / "Iron" / "Cuirass.dds", makeDds());
    writeFile(root / "scripts" / "test.pex", "pex data");
    writeFile(root / "sound" / "fx" / "hit.wav", "RIFF....WAVE");
  }
  const auto expected   = looseFiles(root);
  const fs::path archive = dir.path() / (std::string("out.") + format.extension);

  pack(root, format.type, format.compressible, archive);
  ASSERT_TRUE(fs::is_regular_file(archive));

  const auto packed = readWithLibbsarchpp(archive);
  ASSERT_EQ(packed.size(), expected.size());
  for (const auto& [path, content] : expected) {
    ASSERT_TRUE(packed.contains(path)) << path;
    if (format.textures) {
      // the archive keeps the DDS information, not the header: the pixel data must match
      ASSERT_GE(packed.at(path).size(), 2048u) << path;
      EXPECT_EQ(packed.at(path).substr(packed.at(path).size() - 2048),
                content.substr(content.size() - 2048))
          << path;
    } else {
      EXPECT_EQ(packed.at(path), content) << path;
    }
  }

  if (format.bsatkReads) {
    EXPECT_EQ(readWithBsatk(archive, dir.path() / "bsatk"), expected);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Formats, BsArchiveAutoFormat,
    testing::Values(Format{"TES3", baTES3, "bsa", false, false, false},
                    Format{"TES4", baTES4, "bsa", false, true, true},
                    Format{"TES4", baTES4, "bsa", false, true, false},
                    Format{"FO3", baFO3, "bsa", false, true, true},
                    Format{"FO3", baFO3, "bsa", false, true, false},
                    Format{"SSE", baSSE, "bsa", false, true, true},
                    Format{"SSE", baSSE, "bsa", false, true, false},
                    Format{"FO4", baFO4, "ba2", false, true, true},
                    Format{"FO4", baFO4, "ba2", false, true, false},
                    Format{"FO4dds", baFO4dds, "ba2", true, false, true},
                    Format{"SF", baSF, "ba2", false, false, true},
                    Format{"SFdds", baSFdds, "ba2", true, false, true}),
    formatName);

// set_compressed(true) has to shrink a compressible file, not only flag the archive
TEST(BsArchiveAuto, CompressionShrinksTheArchive)
{
  TempDir dir;
  const fs::path root = makeMod(dir.path());
  for (const auto type : {baTES4, baFO3, baSSE, baFO4, baSF}) {
    const fs::path compressed = dir.path() / ("compressed-" + std::to_string(type));
    const fs::path stored     = dir.path() / ("stored-" + std::to_string(type));
    pack(root, type, true, compressed);
    pack(root, type, false, stored);
    EXPECT_LT(fs::file_size(compressed), fs::file_size(stored) - 4096) << "type " << type;
    EXPECT_EQ(readWithLibbsarchpp(compressed), looseFiles(root)) << "type " << type;
  }
}

TEST(BsArchiveAuto, NonAsciiNamesRoundTrip)
{
  TempDir dir;
  const fs::path root = dir.path() / "mods" / "Café";
  writeFile(root / "textures" / "café" / "ação.dds", makeDds());
  const fs::path archive = dir.path() / "out.bsa";

  pack(root, baSSE, true, archive);

  EXPECT_EQ(readWithLibbsarchpp(archive), looseFiles(root));
}

TEST(BsArchiveAuto, EmptyArchiveIsNotCreated)
{
  TempDir dir;
  const fs::path archive = dir.path() / "out.bsa";
  libbsarch::bs_archive_auto bsa(baSSE);

  EXPECT_THROW(bsa.save_to_disk(archive), std::runtime_error);
  EXPECT_FALSE(fs::exists(archive));
}

TEST(BsArchiveAuto, UnwritableTargetLeavesNoFile)
{
  if (geteuid() == 0) {
    GTEST_SKIP() << "root ignores the permissions of the target directory";
  }
  TempDir dir;
  const fs::path root = makeMod(dir.path());
  const fs::path target = dir.path() / "readonly";
  fs::create_directories(target);
  fs::permissions(target, fs::perms::owner_read | fs::perms::owner_exec);
  const fs::path archive = target / "out.bsa";

  EXPECT_THROW(pack(root, baSSE, true, archive), std::runtime_error);
  EXPECT_FALSE(fs::exists(archive));
}

TEST(BsArchiveAuto, EmptyPackKeepsAnExistingArchive)
{
  TempDir dir;
  const fs::path archive = dir.path() / "My Test Mod - Textures.ba2";
  writeFile(archive, "an archive that was already there");

  libbsarch::bs_archive_auto bsa(baFO4dds);
  EXPECT_THROW(bsa.save_to_disk(archive), std::runtime_error);

  EXPECT_EQ(readFile(archive), "an archive that was already there");
}
