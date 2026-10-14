// Linux implementations of the Win32-only functions of envmodule.cpp (the rest of
// envmodule.cpp is shared).
//
// - processes are read from /proc/<pid>/stat (parent pid, comm) and /proc/<pid>/exe
//   (the name of a process is the file name of its executable, or its comm when the
//   executable cannot be read, for processes of other users and kernel threads),
// - the HANDLE of a process is its pid (see HandleCloser in envmodule.h), there are no
//   jobs,
// - the loaded modules are the files mapped as executable in /proc/self/maps (the
//   executable and the shared libraries); they have no version information, their
//   timestamp is the modification time of the file.

#include "../envmodule.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>

#include <signal.h>
#include <sys/types.h>

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <uibase/log.h>

using namespace MOBase;

namespace env
{

// envmodule.cpp
void findChildren(Process& parent, const std::vector<Process>& processes);

namespace
{

  DWORD pidFromHandle(HANDLE h)
  {
    if (h == INVALID_HANDLE_VALUE) {
      return 0;
    }

    return static_cast<DWORD>(reinterpret_cast<std::intptr_t>(h));
  }

  HANDLE handleFromPid(DWORD pid)
  {
    return reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(pid));
  }

  struct ProcStat
  {
    DWORD ppid = 0;
    QString comm;
  };

  // reads /proc/<pid>/stat, "pid (comm) state ppid ...", where comm can contain
  // spaces and parentheses
  //
  std::optional<ProcStat> readStat(DWORD pid)
  {
    std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
    if (!in) {
      return {};
    }

    std::string line;
    std::getline(in, line);

    const auto open  = line.find('(');
    const auto close = line.rfind(')');

    if (open == std::string::npos || close == std::string::npos || close < open) {
      return {};
    }

    std::istringstream rest(line.substr(close + 1));
    std::string state;
    long long ppid = 0;

    if (!(rest >> state >> ppid)) {
      return {};
    }

    ProcStat s;
    s.ppid = static_cast<DWORD>(ppid);
    s.comm = QString::fromStdString(line.substr(open + 1, close - open - 1));

    return s;
  }

  // file name of the executable of the process, empty if it can't be read
  //
  QString exeName(DWORD pid)
  {
    std::error_code ec;
    const auto exe =
        std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", ec);

    if (ec || exe.empty()) {
      return {};
    }

    auto name = QString::fromStdString(exe.filename().string());

    // the executable has been deleted or replaced since the process started
    const QString deleted = " (deleted)";
    if (name.endsWith(deleted)) {
      name.chop(deleted.size());
    }

    return name;
  }

  std::optional<Process> readProcess(DWORD pid)
  {
    const auto stat = readStat(pid);
    if (!stat) {
      return {};
    }

    auto name = exeName(pid);
    if (name.isEmpty()) {
      name = stat->comm;
    }

    return Process(pid, stat->ppid, name);
  }

  bool processExists(DWORD pid)
  {
    if (pid == 0) {
      return false;
    }

    return (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
  }

}  // namespace

Module::Module(QString path, std::size_t fileSize)
    : m_path(std::move(path)), m_fileSize(fileSize)
{
  m_timestamp = QFileInfo(m_path).lastModified();
}

bool Module::interesting() const
{
  // the libraries of the system, like the Windows directory on Windows
  for (const auto* dir : {"/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/"}) {
    if (m_path.startsWith(QString::fromLatin1(dir))) {
      return false;
    }
  }

  return true;
}

Process::Process(HANDLE h) : Process(pidFromHandle(h), 0, {}) {}

HandlePtr Process::openHandleForWait() const
{
  // nothing is opened, the handle is the pid; empty if the process doesn't exist
  if (!processExists(m_pid)) {
    return {};
  }

  return HandlePtr(handleFromPid(m_pid));
}

bool Process::canAccess() const
{
  // like OpenProcess() on Windows, only fails when access is denied: the process
  // belongs to another user (or root)
  if (m_pid == 0) {
    return true;
  }

  if (::kill(static_cast<pid_t>(m_pid), 0) != 0 && errno == EPERM) {
    return false;
  }

  return true;
}

std::vector<Module> getLoadedModules()
{
  std::ifstream maps("/proc/self/maps");
  if (!maps) {
    log::error("can't read /proc/self/maps");
    return {};
  }

  // "address perms offset dev inode path", the path is optional and can contain
  // spaces
  std::set<QString> paths;
  std::string line;

  while (std::getline(maps, line)) {
    std::istringstream iss(line);
    std::string address, perms, offset, dev, inode;

    if (!(iss >> address >> perms >> offset >> dev >> inode)) {
      continue;
    }

    // only code: the executable and the shared libraries, not the data files
    if (perms.find('x') == std::string::npos) {
      continue;
    }

    std::string path;
    std::getline(iss >> std::ws, path);

    if (path.empty() || path[0] != '/') {
      // anonymous mappings, [vdso], [stack], ...
      continue;
    }

    paths.insert(QString::fromStdString(path));
  }

  std::vector<Module> v;

  for (auto&& path : paths) {
    const QFileInfo fi(path);
    if (!fi.isFile()) {
      // deleted, memfd, ...
      continue;
    }

    v.push_back(Module(path, static_cast<std::size_t>(fi.size())));
  }

  // sorting by display name
  std::sort(v.begin(), v.end(), [](auto&& a, auto&& b) {
    return (a.displayPath().compare(b.displayPath(), Qt::CaseInsensitive) < 0);
  });

  return v;
}

std::vector<Process> getRunningProcesses()
{
  std::vector<Process> v;

  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator("/proc", ec)) {
    const auto name = e.path().filename().string();

    if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) {
      // not a process
      continue;
    }

    // the process may have exited in the meantime
    if (auto p = readProcess(static_cast<DWORD>(std::stoul(name)))) {
      v.push_back(std::move(*p));
    }
  }

  if (ec) {
    log::error("can't list /proc, {}", ec.message());
  }

  return v;
}

Process getProcessTree(HANDLE h)
{
  Process root;

  const auto parentPID = pidFromHandle(h);
  const auto v         = getRunningProcesses();

  for (auto&& p : v) {
    if (p.pid() == parentPID) {
      Process child = p;
      findChildren(child, v);
      root.addChild(child);
      break;
    }
  }

  return root;
}

QString getProcessName(DWORD pid)
{
  const auto p = readProcess(pid);

  if (!p) {
    log::error("can't get name of process {}, it doesn't exist", pid);
    return {};
  }

  return p->name();
}

QString getProcessName(HANDLE process)
{
  const QString badName = "unknown";

  if (process == 0 || process == INVALID_HANDLE_VALUE) {
    return badName;
  }

  const auto p = readProcess(pidFromHandle(process));
  if (!p) {
    return badName;
  }

  return p->name();
}

DWORD getProcessParentID(DWORD pid)
{
  const auto stat = readStat(pid);
  return (stat ? stat->ppid : 0);
}

DWORD getProcessParentID(HANDLE handle)
{
  return getProcessParentID(pidFromHandle(handle));
}

}  // namespace env
