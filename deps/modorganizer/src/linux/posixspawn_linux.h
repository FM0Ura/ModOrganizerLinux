#ifndef MO2_POSIXSPAWN_LINUX_H
#define MO2_POSIXSPAWN_LINUX_H

#include <string>
#include <sys/types.h>
#include <vector>

// posix_spawn() for spawn_linux.cpp, see posixspawn_linux.cpp
namespace posixspawn
{

// starts `program` (an absolute path) with `arguments` (argv[1]...) in
// `workingDirectory`, with the environment of this process, in a new process group
// whose id is the pid of the program; returns 0 and sets `pid`, or returns the errno of
// the failure (the program not starting, a working directory that can't be entered,
// ...)
int startInNewProcessGroup(const std::string& program,
                           const std::vector<std::string>& arguments,
                           const std::string& workingDirectory, pid_t& pid);

}  // namespace posixspawn

#endif  // MO2_POSIXSPAWN_LINUX_H
