// Tests of the Linux bs_archive_auto (deps/bsapacker/linux, spec of Phase 4e, sections
// 3.2 and 5.3): archives written through the libbsarch-compatible API read back with
// libbsarchpp and, where it reads the format, with bsatk.
#include <libbsarch/bs_archive_auto.hpp>
#include <libbsarchpp/Bsa.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

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
  libbsarchpp::Bsa bsa(archive);
  std::map<std::string, std::string> files;
  for (const auto& name : bsa.getFileList()) {
    const auto data = bsa.extractFileData(name);
    files[normalized(name.generic_string())] = std::string(data.begin(), data.end());
  }
  return files;
}

}  // namespace

TEST(BsArchiveAuto, SkyrimSERoundTrip)
{
  TempDir dir;
  const fs::path root    = makeMod(dir.path());
  const fs::path archive = dir.path() / "My Test Mod.bsa";
  pack(root, baSSE, true, archive);

  ASSERT_TRUE(fs::is_regular_file(archive));
  EXPECT_EQ(readWithLibbsarchpp(archive), looseFiles(root));
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
