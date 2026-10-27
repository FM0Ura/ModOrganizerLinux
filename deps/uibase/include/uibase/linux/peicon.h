// Icon embedded in PE executables (what ExtractIcon provides on Windows), Linux only.
// The file is untrusted input: every offset and size is checked.

#ifndef UIBASE_LINUX_PEICON_H
#define UIBASE_LINUX_PEICON_H

#include <uibase/dllimport.h>

#include <QByteArray>
#include <QImage>
#include <QString>

#include <optional>
#include <vector>

namespace MOBase::details
{

// The contents of an .ico file (ICONDIR, ICONDIRENTRYs and images) assembled from the
// first RT_GROUP_ICON of the file and the RT_ICON images it references. nullopt if the
// file cannot be read, is not a PE file, has no icon group or none of its images can be
// read. At most 64 entries per group and 1 MiB per image are considered.
QDLLEXPORT std::optional<QByteArray> readPeIcon(const QString& path);

// All the images of the icon embedded in the file (see readPeIcon), decoded with the
// ico image format plugin; empty if there is none. The result is cached in memory per
// path, modification time and size (including the "no icon" result); thread safe.
QDLLEXPORT std::vector<QImage> peIconImages(const QString& path);

}  // namespace MOBase::details

#endif  // UIBASE_LINUX_PEICON_H
