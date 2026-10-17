// Linux implementation of envfs.h (the Windows one, envfs.cpp, uses
// NtQueryDirectoryFile and is not built on Linux).
//
// Directories are read with getdents64() on file descriptors opened with
// openat(O_DIRECTORY), relative to their parent, with an explicit stack instead of
// recursion. Like on Windows, the callbacks get the names of the entries (not their
// paths) and directories are reported depth-first, between dirStartF() and dirEndF().
//
// - names are UTF-8 on disk and converted to std::wstring for the callbacks,
// - times are FILETIME: 100 ns intervals since 1601-01-01 UTC, from st_mtim,
// - symbolic links are followed (to files and directories); broken links, links to
//   a directory being walked (loops) and directories that cannot be opened (EACCES,
//   ...) are skipped with a warning; a directory is entered once per descent: a link to
//   a directory that is not being walked is followed, a link back to one is not,
// - system calls interrupted by a signal (EINTR) are retried.

#include "../envfs.h"
#include "../shared/util.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stack>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <QString>

#include <uibase/linux/filetime.h>
#include <uibase/log.h>

using namespace MOBase;

namespace env
{

namespace
{

  // size of the buffer given to getdents64(), one per depth level
  constexpr std::size_t BufferSize = 64 * 1024;

  // the record returned by getdents64(), see getdents(2); not every C library declares
  // it
  struct LinuxDirent64
  {
    std::uint64_t d_ino;
    std::int64_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[1];
  };

  std::wstring toWString(const char* name)
  {
    return QString::fromUtf8(name).toStdWString();
  }

  bool isDotDir(const char* name)
  {
    return (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)));
  }

  // calls f() again for as long as it fails with EINTR (a signal handler installed
  // without SA_RESTART interrupted it); f returns -1 and sets errno on failure
  template <class F>
  auto retryOnEintr(F&& f)
  {
    for (;;) {
      const auto r = f();
      if (!(r < 0 && errno == EINTR)) {
        return r;
      }
    }
  }

  // a directory being read
  struct Frame
  {
    int fd;

    // name given to dirStartF(), empty for the walked directory itself
    std::wstring name;

    // full path, for log messages
    std::string path;

    // identity of the directory, to detect symbolic link loops
    dev_t dev;
    ino_t ino;

    unsigned char* buffer;
    std::size_t size = 0;  // bytes in the buffer
    std::size_t pos  = 0;  // offset of the next entry
  };

  // closes the directories still open if a callback throws
  struct FrameStack
  {
    std::vector<Frame> frames;

    ~FrameStack()
    {
      for (auto& f : frames) {
        ::close(f.fd);
      }
    }
  };

}  // namespace

void setHandleCloserThreadCount(std::size_t)
{
  // nothing to do: on Windows, closing handles is slow enough to be done by a pool of
  // threads, here a directory is closed as soon as it has been read
}

void DirectoryWalker::forEachEntry(const std::wstring& path, void* cx,
                                   DirStartF* dirStartF, DirEndF* dirEndF, FileF* fileF)
{
  auto bufferFor = [&](std::size_t depth) {
    while (m_buffers.size() <= depth) {
      m_buffers.emplace_back(std::make_unique<unsigned char[]>(BufferSize));
    }

    return m_buffers[depth].get();
  };

  const std::string rootPath = QString::fromStdWString(path).toStdString();

  const int rootFd = retryOnEintr([&] {
    return ::open(rootPath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  });
  if (rootFd < 0) {
    log::error("failed to open directory '{}': {}", rootPath, std::strerror(errno));
    return;
  }

  FrameStack stack;

  {
    struct stat st = {};
    if (retryOnEintr([&] {
          return ::fstat(rootFd, &st);
        }) != 0) {
      log::error("failed to examine directory '{}': {}", rootPath,
                 std::strerror(errno));
      ::close(rootFd);
      return;
    }

    stack.frames.push_back({rootFd, {}, rootPath, st.st_dev, st.st_ino, bufferFor(0)});
  }

  while (!stack.frames.empty()) {
    Frame& top = stack.frames.back();

    if (top.pos >= top.size) {
      // buffer exhausted, read more entries
      const long n = retryOnEintr([&] {
        return ::syscall(SYS_getdents64, top.fd, top.buffer, BufferSize);
      });

      if (n > 0) {
        top.size = static_cast<std::size_t>(n);
        top.pos  = 0;
        continue;
      }

      if (n < 0) {
        log::error("failed to read directory '{}': {}", top.path, std::strerror(errno));
      }

      // end of this directory
      ::close(top.fd);

      const std::wstring name = std::move(top.name);
      stack.frames.pop_back();

      if (!stack.frames.empty()) {
        dirEndF(cx, name);
      }

      continue;
    }

    const auto* entry = reinterpret_cast<const LinuxDirent64*>(top.buffer + top.pos);
    top.pos += entry->d_reclen;

    const char* name = entry->d_name;
    if (isDotDir(name)) {
      continue;
    }

    unsigned char type = entry->d_type;
    struct stat st     = {};

    if (type != DT_DIR) {
      // files need their size and time; symbolic links are followed and entries of
      // unknown type (some file systems do not report it) are resolved
      const int flags = (type == DT_REG ? AT_SYMLINK_NOFOLLOW : 0);

      if (retryOnEintr([&] {
            return ::fstatat(top.fd, name, &st, flags);
          }) != 0) {
        const int e = errno;
        struct stat lst;

        if (retryOnEintr([&] {
              return ::fstatat(top.fd, name, &lst, AT_SYMLINK_NOFOLLOW);
            }) == 0 &&
            S_ISLNK(lst.st_mode)) {
          log::warn("skipping broken symbolic link '{}/{}'", top.path, name);
        } else {
          log::warn("skipping '{}/{}': {}", top.path, name, std::strerror(e));
        }

        continue;
      }

      if (S_ISDIR(st.st_mode)) {
        type = DT_DIR;
      } else if (S_ISREG(st.st_mode)) {
        type = DT_REG;
      } else {
        // fifos, sockets, devices: nothing that a game would load
        continue;
      }
    }

    if (type == DT_REG) {
      fileF(cx, toWString(name), MOBase::linux::toFileTime(st.st_mtim),
            static_cast<std::uint64_t>(st.st_size));
      continue;
    }

    // directory; like on Windows, they are not walked without directory callbacks
    if (!dirStartF || !dirEndF) {
      continue;
    }

    const std::string subPath = top.path + "/" + name;

    const int fd = retryOnEintr([&] {
      return ::openat(top.fd, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    });
    if (fd < 0) {
      log::warn("skipping directory '{}': {}", subPath, std::strerror(errno));
      continue;
    }

    struct stat dst = {};
    if (retryOnEintr([&] {
          return ::fstat(fd, &dst);
        }) != 0) {
      log::warn("skipping directory '{}': {}", subPath, std::strerror(errno));
      ::close(fd);
      continue;
    }

    const bool loop =
        std::any_of(stack.frames.begin(), stack.frames.end(), [&](const Frame& f) {
          return f.dev == dst.st_dev && f.ino == dst.st_ino;
        });

    if (loop) {
      log::warn("skipping '{}': it is a symbolic link to a directory being walked",
                subPath);
      ::close(fd);
      continue;
    }

    std::wstring wname      = toWString(name);
    const std::size_t depth = stack.frames.size();

    try {
      stack.frames.push_back(
          {fd, std::move(wname), subPath, dst.st_dev, dst.st_ino, bufferFor(depth)});
    } catch (...) {
      ::close(fd);
      throw;
    }

    // `top` is invalid from here
    dirStartF(cx, stack.frames.back().name);
  }
}

void forEachEntry(const std::wstring& path, void* cx, DirStartF* dirStartF,
                  DirEndF* dirEndF, FileF* fileF)
{
  DirectoryWalker().forEachEntry(path, cx, dirStartF, dirEndF, fileF);
}

Directory getFilesAndDirs(const std::wstring& path)
{
  struct Context
  {
    std::stack<Directory*> current;
  };

  Directory root;

  Context cx;
  cx.current.push(&root);

  env::forEachEntry(
      path, &cx,
      [](void* pcx, std::wstring_view path) {
        Context* cx = (Context*)pcx;

        cx->current.top()->dirs.push_back(Directory(path));
        cx->current.push(&cx->current.top()->dirs.back());
      },

      [](void* pcx, std::wstring_view) {
        Context* cx = (Context*)pcx;
        cx->current.pop();
      },

      [](void* pcx, std::wstring_view path, FILETIME ft, uint64_t s) {
        Context* cx = (Context*)pcx;

        cx->current.top()->files.push_back(File(path, ft, s));
      });

  return root;
}

Directory getFilesAndDirsWithFind(const std::wstring& path)
{
  // there is no FindFirstFileEx() here, both are the same walk
  return getFilesAndDirs(path);
}

File::File(std::wstring_view n, FILETIME ft, uint64_t s)
    : name(n.begin(), n.end()), lcname(MOShared::ToLowerCopy(name)), lastModified(ft),
      size(s)
{}

Directory::Directory() {}

Directory::Directory(std::wstring_view n)
    : name(n.begin(), n.end()), lcname(MOShared::ToLowerCopy(name))
{}

}  // namespace env
