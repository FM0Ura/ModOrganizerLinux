#ifndef MO2_TESTS_TEMPDIR_H
#define MO2_TESTS_TEMPDIR_H

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

// a temporary directory, removed (with everything in it) on destruction
//
class TempDir
{
public:
  TempDir()
  {
    std::string tpl =
        (std::filesystem::temp_directory_path() / "mo2-tests-XXXXXX").string();

    if (!::mkdtemp(tpl.data())) {
      throw std::runtime_error("mkdtemp failed");
    }

    m_path = tpl;
  }

  ~TempDir()
  {
    // directories made unreadable by a test must be traversable again to be removed
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(
             m_path, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_directory(ec) && !it->is_symlink(ec)) {
        std::filesystem::permissions(it->path(), std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add, ec);
      }
    }

    std::filesystem::remove_all(m_path, ec);
  }

  TempDir(const TempDir&)            = delete;
  TempDir& operator=(const TempDir&) = delete;

  const std::filesystem::path& path() const { return m_path; }

  // creates a file of the given size at the given relative path, with its parent
  // directories
  //
  std::filesystem::path createFile(const std::filesystem::path& relative,
                                   std::size_t size = 0) const
  {
    const auto p = m_path / relative;
    std::filesystem::create_directories(p.parent_path());

    std::ofstream out(p, std::ios::binary);
    out << std::string(size, 'x');

    return p;
  }

private:
  std::filesystem::path m_path;
};

#endif  // MO2_TESTS_TEMPDIR_H
