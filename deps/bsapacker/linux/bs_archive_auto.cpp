#include <libbsarch/bs_archive_auto.hpp>

#include <libbsarchpp/Bsa.h>
#include <libbsarchpp/enums.h>
#include <libbsarchpp/types.h>

#include <QDebug>
#include <QString>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

namespace fs = std::filesystem;

namespace libbsarch
{

namespace
{
  // UTF-8; libstdc++ already converts a std::wstring path to UTF-8 whatever the locale,
  // this keeps the conversion explicit
  fs::path utf8Path(const std::wstring& path)
  {
    return fs::path(QString::fromStdWString(path).toStdString());
  }

  // the part of absolute after root, as libbsarchpp's Bsa::addFile strips it
  fs::path inArchive(const fs::path& root, const fs::path& absolute)
  {
    const std::string rootString = root.generic_string();
    std::size_t pos              = rootString.length();
    if (pos > 1 && !rootString.ends_with('/')) {
      ++pos;
    }
    return fs::path(absolute.generic_string().substr(pos));
  }

  QString toQString(const fs::path& path)
  {
    return QString::fromStdString(path.string());
  }

  // ASCII lower case, as the games look the names up
  fs::path toLower(const fs::path& path)
  {
    std::string s = path.generic_string();
    std::transform(s.begin(), s.end(), s.begin(), [](char c) {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return fs::path(s);
  }

  libbsarchpp::Buffer readFile(const fs::path& path)
  {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      throw std::runtime_error(std::string("could not open the file for reading: ") +
                               std::strerror(errno));
    }
    libbsarchpp::Buffer data((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
    if (in.bad()) {
      throw std::runtime_error("could not read the file");
    }
    return data;
  }

  // the .bsa types (TES3 to SSE) get their names in lower case, as the games look them
  // up: for TES4 to SSE, libbsarchpp 1.0.7 hashes the extension in its own case
  // (CreateHashTES4); it also takes a name without an extension for a folder that it
  // cannot find (findFileRecordTES4), so such files are left out
  bool isBsa(libbsarchpp::ArchiveType type)
  {
    return type == libbsarchpp::TES3 || type == libbsarchpp::TES4 ||
           type == libbsarchpp::FO3 || type == libbsarchpp::SSE;
  }

  libbsarchpp::ArchiveType toLibbsarchpp(bsa_archive_type_t type)
  {
    switch (type) {
    case baTES3:
      return libbsarchpp::TES3;
    case baTES4:
      return libbsarchpp::TES4;
    case baFO3:
      return libbsarchpp::FO3;
    case baSSE:
      return libbsarchpp::SSE;
    case baFO4:
      return libbsarchpp::FO4;
    case baFO4dds:
      return libbsarchpp::FO4dds;
    case baSF:
      return libbsarchpp::SF;
    case baSFdds:
      return libbsarchpp::SFdds;
    case baNone:
    default:
      throw std::runtime_error("no archive type");
    }
  }
}  // namespace

disk_blob::disk_blob(const std::wstring& root, const std::wstring& absolute)
    : disk_blob(utf8Path(root), utf8Path(absolute))
{}

disk_blob::disk_blob(const fs::path& root, const fs::path& absolute)
    : root_dir(root), path_on_disk(absolute), path_in_archive(inArchive(root, absolute))
{}

bs_archive_auto::bs_archive_auto(const bsa_archive_type_t& type) : m_type(type) {}

void bs_archive_auto::add_file_from_disk(const disk_blob& blob)
{
  m_files.push_back(blob);
}

void bs_archive_auto::set_compressed(bool value)
{
  m_compressed = value;
}

void bs_archive_auto::set_share_data(bool value)
{
  m_shareData = value;
}

void bs_archive_auto::set_dds_callback(bsa_file_dds_info_proc_t, const std::wstring& root)
{
  m_ddsRoot = utf8Path(root);
}

void bs_archive_auto::save_to_disk(const fs::path& archive_path)
{
  // nothing is written before these checks: they fail without touching the file, which
  // may be an archive that was already there
  const auto noFilesToPack = [&archive_path]() {
    qDebug().noquote() << QString("no files to pack into %1").arg(toQString(archive_path));
    return std::runtime_error("no files to pack");
  };
  if (m_files.empty()) {
    throw noFilesToPack();
  }
  const libbsarchpp::ArchiveType type = toLibbsarchpp(m_type);

  // the files to pack and their names in the archive: in a .bsa, in lower case, without
  // the files that have no extension and with only the first of the names that differ
  // in case alone
  std::vector<std::pair<const disk_blob*, fs::path>> files;
  files.reserve(m_files.size());
  if (isBsa(type)) {
    std::unordered_set<std::string> names;
    for (const auto& file : m_files) {
      const fs::path name = toLower(file.path_in_archive);
      if (!name.has_extension()) {
        qWarning().noquote() << QString("%1 is not packed into %2: it has no extension")
                                    .arg(toQString(file.path_on_disk))
                                    .arg(toQString(archive_path));
      } else if (!names.insert(name.generic_string()).second) {
        qWarning().noquote()
            << QString("%1 is not packed into %2: %3 is already in the archive")
                   .arg(toQString(file.path_on_disk))
                   .arg(toQString(archive_path))
                   .arg(toQString(name));
      } else {
        files.emplace_back(&file, name);
      }
    }
  } else {
    for (const auto& file : m_files) {
      files.emplace_back(&file, file.path_in_archive);
    }
  }

  if (files.empty()) {
    throw noFilesToPack();
  }

  // a failure removes the partial file, but not a file that was there before (or may
  // have been) and that the writer did not get to open
  std::error_code existsError;
  const bool existedBefore = fs::exists(archive_path, existsError) || existsError;
  bool writerCreated       = false;

  try {
    std::vector<fs::path> fileList;
    fileList.reserve(files.size());
    for (const auto& [file, name] : files) {
      // libbsarchpp writes a name one byte per character when it saves, and fails
      // without saying which name has a character outside Latin-1
      const std::u16string characters = name.generic_u16string();
      if (std::any_of(characters.begin(), characters.end(),
                      [](char16_t c) { return c > 0xFF; })) {
        throw std::runtime_error(name.string() +
                                 ": the name has a character outside Latin-1");
      }
      fileList.push_back(name);
    }

    const std::optional<fs::path> ddsBasePath =
        m_ddsRoot.empty() ? std::nullopt : std::optional<fs::path>(m_ddsRoot);

    // the full constructor: the 4-argument one of libbsarchpp 1.0.7 leaves the object
    // uninitialized (spec of Phase 4e, section 1.3); one thread, for a deterministic
    // archive
    libbsarchpp::Bsa bsa(archive_path, type, fileList, ddsBasePath, m_compressed,
                         m_shareData, false);
    writerCreated = true;  // the constructor opened (truncated) the file

    // libbsarchpp's zlib level starts at 0, which "compresses" into stored blocks: the
    // archive is flagged compressed and no smaller. lz4 (SSE, SFdds) has its own default
    if (type == libbsarchpp::TES4 || type == libbsarchpp::FO3 ||
        type == libbsarchpp::FO4 || type == libbsarchpp::FO4dds ||
        type == libbsarchpp::SF) {
      bsa.setCompressionLevel(6);  // zlib's default level
    }
    for (const auto& [file, name] : files) {
      try {
        if (isBsa(type)) {
          // under the lower-case name of the files list, so that both hashes agree
          bsa.addFile(name, readFile(file->path_on_disk));
        } else {
          bsa.addFile(file->root_dir, file->path_on_disk);
        }
      } catch (const std::exception& e) {
        throw std::runtime_error(name.string() + ": " + e.what());
      }
    }
    bsa.save();
  } catch (const std::exception& e) {
    qWarning().noquote() << QString("failed to write archive %1: %2")
                                .arg(toQString(archive_path))
                                .arg(QString::fromStdString(e.what()));
    if (!existedBefore || writerCreated) {
      std::error_code ec;
      fs::remove(archive_path, ec);
    }
    throw;
  }
}

}  // namespace libbsarch
