// posix_spawn() for spawn_linux.cpp, in a file of its own: the application has src/ in
// its include path, where spawn.h of Mod Organizer hides the <spawn.h> of the system,
// so this file is built by a target without that include path (organizer_posixspawn,
// see src/CMakeLists.txt) and without anything of Qt or Mod Organizer.
//
// posix_spawn() rather than fork() and exec(): Mod Organizer is a big process with
// many threads, glibc starts the program without copying it (vfork-like) and reports
// the failures of the file actions (chdir) and of exec() as the result.

#include "posixspawn_linux.h"

#include <csignal>
#include <spawn.h>

extern char** environ;

namespace posixspawn
{

int startInNewProcessGroup(const std::string& program,
                           const std::vector<std::string>& arguments,
                           const std::string& workingDirectory, pid_t& pid)
{
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 2);
  argv.push_back(const_cast<char*>(program.c_str()));
  for (const auto& a : arguments) {
    argv.push_back(const_cast<char*>(a.c_str()));
  }
  argv.push_back(nullptr);

  posix_spawnattr_t attr;
  if (const int e = ::posix_spawnattr_init(&attr); e != 0) {
    return e;
  }

  posix_spawn_file_actions_t actions;
  if (const int e = ::posix_spawn_file_actions_init(&actions); e != 0) {
    ::posix_spawnattr_destroy(&attr);
    return e;
  }

  // a group of its own (pgroup 0: the pid of the program), the one mo2-vfs watches;
  // the signals that Mod Organizer ignores or blocks are not the program's business
  sigset_t defaults;
  sigfillset(&defaults);
  sigdelset(&defaults, SIGKILL);
  sigdelset(&defaults, SIGSTOP);

  sigset_t mask;
  sigemptyset(&mask);

  int e = ::posix_spawnattr_setflags(
      &attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
  if (e == 0) {
    e = ::posix_spawnattr_setpgroup(&attr, 0);
  }
  if (e == 0) {
    e = ::posix_spawnattr_setsigdefault(&attr, &defaults);
  }
  if (e == 0) {
    e = ::posix_spawnattr_setsigmask(&attr, &mask);
  }
  if (e == 0) {
    e = ::posix_spawn_file_actions_addchdir_np(&actions, workingDirectory.c_str());
  }
  if (e == 0) {
    e = ::posix_spawn(&pid, program.c_str(), &actions, &attr, argv.data(), environ);
  }

  ::posix_spawn_file_actions_destroy(&actions);
  ::posix_spawnattr_destroy(&attr);

  return e;
}

}  // namespace posixspawn
