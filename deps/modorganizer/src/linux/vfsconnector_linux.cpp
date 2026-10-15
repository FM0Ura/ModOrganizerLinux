// Linux implementation of the virtual file system connector (vfsconnector.h).
//
// There is no virtual file system on Linux yet, and programs can't be started (Phase
// 3), so NullVfsConnector only keeps the last mappings it was given: they are the
// input of the Linux file system of Phase 2. The paths of the mappings are disk paths;
// the ones built by OrganizerCore::fileMapping() from the data tree and the ones given
// by plugins (local saves, file mappers) may use backslashes, which are converted to
// slashes here (MOShared::normalizeSeparators()).

#include "../shared/util.h"
#include "../vfsconnector.h"

using namespace MOBase;

std::unique_ptr<VfsConnector> createVfsConnector()
{
  return std::make_unique<NullVfsConnector>();
}

void NullVfsConnector::updateMapping(const MappingType& mapping)
{
  m_mapping.clear();
  m_mapping.reserve(mapping.size());

  for (const auto& m : mapping) {
    m_mapping.push_back({MOShared::normalizeSeparators(m.source),
                         MOShared::normalizeSeparators(m.destination), m.isDirectory,
                         m.createTarget});
  }

  log::debug("no virtual file system on Linux, {} mappings kept", m_mapping.size());
}

void NullVfsConnector::updateParams(log::Levels, env::CoreDumpTypes, const QString&,
                                    std::chrono::seconds, QString, const QStringList&,
                                    const QStringList&)
{
  // nothing to configure: the parameters (log level, crash dumps, spawn delay,
  // blacklisted executables, skipped files and directories) are those of usvfs
}

void NullVfsConnector::updateForcedLibraries(
    const QList<ExecutableForcedLoadSetting>& forcedLibraries)
{
  // forced libraries are DLLs injected by usvfs into the started programs
  if (!forcedLibraries.isEmpty()) {
    log::debug("forced libraries are not supported on Linux, {} ignored",
               forcedLibraries.size());
  }
}

QString NullVfsConnector::name() const
{
  return "none";
}

const MappingType& NullVfsConnector::lastMapping() const
{
  return m_mapping;
}
