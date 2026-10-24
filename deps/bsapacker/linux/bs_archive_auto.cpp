#include <libbsarch/bs_archive_auto.hpp>

#include <libbsarchpp/Bsa.h>
#include <libbsarchpp/enums.h>

#include <QDebug>
#include <QString>

#include <optional>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace libbsarch
{

namespace
{
  // UTF-8, whatever the locale (std::filesystem::path(std::wstring) converts with it)
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
  if (m_files.empty()) {
    qDebug().noquote() << QString("no files to pack into %1")
                              .arg(QString::fromStdString(archive_path.string()));
    throw std::runtime_error("no files to pack");
  }
  const libbsarchpp::ArchiveType type = toLibbsarchpp(m_type);

  // a failure removes the partial file, but not a file that was there before and that
  // the writer did not get to open
  const bool existedBefore = fs::exists(archive_path);
  bool writerCreated       = false;

  try {
    std::vector<fs::path> fileList;
    fileList.reserve(m_files.size());
    for (const auto& file : m_files) {
      fileList.push_back(file.path_in_archive);
    }

    const std::optional<fs::path> ddsBasePath =
        m_ddsRoot.empty() ? std::nullopt : std::optional<fs::path>(m_ddsRoot);

    // the full constructor: the 4-argument one of libbsarchpp 1.0.7 leaves the object
    // uninitialized (spec of Phase 4e, section 1.3); one thread, for a deterministic
    // archive
    libbsarchpp::Bsa bsa(archive_path, type, fileList, ddsBasePath, m_compressed,
                         m_shareData, false);
    writerCreated = true;  // the constructor opened (truncated) the file
    for (const auto& file : m_files) {
      bsa.addFile(file.root_dir, file.path_on_disk);
    }
    bsa.save();
  } catch (const std::exception& e) {
    qWarning().noquote() << QString("failed to write archive %1: %2")
                                .arg(QString::fromStdString(archive_path.string()))
                                .arg(QString::fromStdString(e.what()));
    if (!existedBefore || writerCreated) {
      std::error_code ec;
      fs::remove(archive_path, ec);
    }
    throw;
  }
}

}  // namespace libbsarch
