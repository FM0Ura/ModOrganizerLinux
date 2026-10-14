// Linux implementations of the Win32-only functions of envsecurity.cpp (the rest of
// envsecurity.cpp is shared).
//
// - there is no Windows Security Center: no security products are reported,
// - the rights on a file are the access(2) checks for the current process, the owner
//   is the owner of the file.

#include "../envsecurity.h"

#include <cerrno>
#include <cstring>

#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QFile>

namespace env
{

QString SecurityProduct::providerToString() const
{
  // never called, getSecurityProducts() reports nothing
  return QString("provider %1").arg(m_provider);
}

std::vector<SecurityProduct> getSecurityProducts()
{
  return {};
}

FileSecurity getFileSecurity(const QString& path)
{
  FileSecurity fs;

  const QByteArray native = QFile::encodeName(path);

  struct stat st = {};
  if (::stat(native.constData(), &st) != 0) {
    const int e = errno;
    fs.error    = QString("can't get information on '%1', %2")
                      .arg(path)
                      .arg(QString::fromLocal8Bit(std::strerror(e)));
    return fs;
  }

  const bool read    = (::access(native.constData(), R_OK) == 0);
  const bool write   = (::access(native.constData(), W_OK) == 0);
  const bool execute = (::access(native.constData(), X_OK) == 0);

  if (read) {
    fs.rights.list.push_back("read");
  }

  if (write) {
    fs.rights.list.push_back("write");
  }

  if (execute) {
    fs.rights.list.push_back("execute");
  }

  fs.rights.hasExecute   = execute;
  fs.rights.normalRights = (read && write && execute);

  if (st.st_uid == ::geteuid()) {
    fs.owner = "(this user)";
  } else if (const auto* pw = ::getpwuid(st.st_uid)) {
    fs.owner = QString::fromLocal8Bit(pw->pw_name);
  } else {
    fs.owner = QString("uid %1").arg(st.st_uid);
  }

  return fs;
}

}  // namespace env
