#include <bsatk/bsatk.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

// Archive paths are stored (and reported by bsatk) with '\' separators.
const char* const kTextureA = "textures\\a.dds";
const char* const kTextureB = "textures\\actors\\b.dds";
const char* const kMesh     = "meshes\\c.nif";

fs::path dataDir()
{
  return fs::path(MO2_BSATK_TEST_DATA);
}

std::string readFile(const fs::path& path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

void collectPaths(const BSA::Folder::Ptr& folder, std::vector<std::string>& paths)
{
  for (unsigned int i = 0; i < folder->getNumFiles(); ++i) {
    paths.push_back(folder->getFile(i)->getFilePath());
  }
  for (unsigned int i = 0; i < folder->getNumSubFolders(); ++i) {
    collectPaths(folder->getSubFolder(i), paths);
  }
}

std::vector<std::string> listFiles(BSA::Archive& archive)
{
  std::vector<std::string> paths;
  collectPaths(archive.getRoot(), paths);
  std::sort(paths.begin(), paths.end());
  return paths;
}

// the archive path (backslashes) as a path below the fixture's files/ directory
fs::path original(const std::string& archivePath)
{
  std::string native = archivePath;
  std::replace(native.begin(), native.end(), '\\', '/');
  return dataDir() / "files" / native;
}

class TempDir
{
public:
  TempDir()
  {
    m_path = fs::temp_directory_path() /
             ("bsatk-test-" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    fs::remove_all(m_path);
    fs::create_directories(m_path);
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(m_path, ec);
  }
  const fs::path& path() const { return m_path; }

private:
  fs::path m_path;
};

}  // namespace

TEST(Bsa, ListsFiles)
{
  BSA::Archive archive;
  // testHashes=true: the name hashes of the fixture must match bsatk's own
  ASSERT_EQ(archive.read((dataDir() / "skyrimse.bsa").string().c_str(), true),
            BSA::ERROR_NONE);

  EXPECT_EQ(archive.getType(), TYPE_SKYRIMSE);
  EXPECT_EQ(listFiles(archive),
            (std::vector<std::string>{kMesh, kTextureA, kTextureB}));
}

TEST(Bsa, BuildsFolderTree)
{
  BSA::Archive archive;
  ASSERT_EQ(archive.read((dataDir() / "skyrimse.bsa").string().c_str(), false),
            BSA::ERROR_NONE);

  BSA::Folder::Ptr root = archive.getRoot();
  ASSERT_EQ(root->getNumSubFolders(), 2u);

  BSA::Folder::Ptr textures;
  for (unsigned int i = 0; i < root->getNumSubFolders(); ++i) {
    if (root->getSubFolder(i)->getName() == "textures") {
      textures = root->getSubFolder(i);
    }
  }
  ASSERT_TRUE(textures);
  EXPECT_EQ(textures->getFullPath(), "textures");
  ASSERT_EQ(textures->getNumFiles(), 1u);
  EXPECT_EQ(textures->getFile(0)->getName(), "a.dds");
  ASSERT_EQ(textures->getNumSubFolders(), 1u);
  EXPECT_EQ(textures->getSubFolder(0)->getFullPath(), "textures\\actors");
}

TEST(Bsa, ExtractsFile)
{
  BSA::Archive archive;
  ASSERT_EQ(archive.read((dataDir() / "skyrimse.bsa").string().c_str(), true),
            BSA::ERROR_NONE);

  TempDir out;
  ASSERT_EQ(archive.extractAll(out.path().string().c_str(),
                               [](int, std::string) {
                                 return true;
                               }),
            BSA::ERROR_NONE);

  for (const char* path : {kTextureA, kTextureB, kMesh}) {
    std::string native = path;
    std::replace(native.begin(), native.end(), '\\', '/');
    const fs::path extracted = out.path() / native;
    ASSERT_TRUE(fs::is_regular_file(extracted)) << extracted;
    const std::string expected = readFile(original(path));
    EXPECT_FALSE(expected.empty());
    EXPECT_EQ(readFile(extracted), expected) << path;
  }
}

TEST(Bsa, ExtractsCompressedFile)
{
  BSA::Archive archive;
  ASSERT_EQ(archive.read((dataDir() / "skyrimse.bsa").string().c_str(), true),
            BSA::ERROR_NONE);

  BSA::File::Ptr mesh;
  for (unsigned int i = 0; i < archive.getRoot()->getNumSubFolders(); ++i) {
    BSA::Folder::Ptr folder = archive.getRoot()->getSubFolder(i);
    if (folder->getName() == "meshes") {
      mesh = folder->getFile(0);
    }
  }
  ASSERT_TRUE(mesh);
  EXPECT_TRUE(archive.compressed(mesh));

  TempDir out;
  ASSERT_EQ(archive.extract(mesh, out.path().string().c_str()), BSA::ERROR_NONE);
  EXPECT_EQ(readFile(out.path() / "c.nif"), readFile(original(kMesh)));
}

TEST(Bsa, MissingFileIsReported)
{
  BSA::Archive archive;
  EXPECT_EQ(archive.read((dataDir() / "missing.bsa").string().c_str(), false),
            BSA::ERROR_FILENOTFOUND);
}
