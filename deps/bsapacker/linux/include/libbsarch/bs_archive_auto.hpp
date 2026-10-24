#pragma once

// Linux replacement of libbsarch's bs_archive_auto (spec of Phase 4e, sections 3.1 and
// 3.2): the calls the bsapacker builders make, written with libbsarchpp. Files are
// collected and the archive is written by save_to_disk().

#include "libbsarch.h"

#include <filesystem>
#include <string>
#include <vector>

namespace libbsarch
{

// a file on disk and its path inside the archive (the absolute path without root_dir,
// computed like libbsarchpp's Bsa::addFile does)
struct disk_blob
{
  disk_blob(const std::wstring& root_dir, const std::wstring& absolute_path);
  disk_blob(const std::filesystem::path& root_dir,
            const std::filesystem::path& absolute_path);

  std::filesystem::path root_dir;
  std::filesystem::path path_on_disk;
  std::filesystem::path path_in_archive;
};

class bs_archive_auto
{
public:
  explicit bs_archive_auto(const bsa_archive_type_t& type);

  void add_file_from_disk(const disk_blob& blob);
  void set_compressed(bool value);
  void set_share_data(bool value);

  // libbsarch reads the DDS information through the callback; libbsarchpp reads the
  // DDS headers itself, so only the root directory (the context) is kept
  void set_dds_callback(bsa_file_dds_info_proc_t dds_function, const std::wstring& root_dir);

  // writes the archive; a .bsa (TES3 to SSE) gets its names in lower case and leaves
  // out, with a warning, the files whose name differs from an earlier one only in case
  // and (but in TES3) the files without an extension; with no files left (or no archive type) throws
  // std::runtime_error and touches nothing; if the writing fails, logs the reason and
  // the file, removes the partial file (not a file that was there before and that the
  // writer never opened) and rethrows
  void save_to_disk(const std::filesystem::path& archive_path);

private:
  bsa_archive_type_t m_type;
  std::vector<disk_blob> m_files;
  std::filesystem::path m_ddsRoot;
  bool m_compressed = false;
  bool m_shareData  = false;
};

}  // namespace libbsarch
