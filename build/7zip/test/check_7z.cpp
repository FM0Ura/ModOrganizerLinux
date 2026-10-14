// Consumer check for the installed 7-Zip package: compiles against the installed
// headers (as the archive library does), dlopens 7z.so and queries its formats.
#include "7zip/Archive/IArchive.h"
#include "7zip/IPassword.h"
#include "Common/MyCom.h"

#include <dlfcn.h>

#include <cstdio>

int main(int argc, char** argv)
{
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s <path to 7z.so>\n", argv[0]);
    return 2;
  }

  void* lib = dlopen(argv[1], RTLD_NOW);
  if (!lib) {
    std::fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return 1;
  }

  using GetNumberOfFormatsFn = UInt32(WINAPI*)(UInt32*);
  using CreateObjectFn       = UInt32(WINAPI*)(const GUID*, const GUID*, void**);

  auto getNumberOfFormats =
      reinterpret_cast<GetNumberOfFormatsFn>(dlsym(lib, "GetNumberOfFormats"));
  auto createObject = reinterpret_cast<CreateObjectFn>(dlsym(lib, "CreateObject"));
  if (!getNumberOfFormats || !createObject) {
    std::fprintf(stderr, "missing export: %s\n", dlerror());
    return 1;
  }

  UInt32 count = 0;
  if (getNumberOfFormats(&count) != S_OK || count == 0) {
    std::fprintf(stderr, "GetNumberOfFormats failed or returned 0\n");
    return 1;
  }

  std::printf("%u\n", count);
  dlclose(lib);
  return 0;
}
