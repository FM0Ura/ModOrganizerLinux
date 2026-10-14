# Minimal CMake package for the system lz4 (vcpkg's lz4 exports lz4::lz4 via a config
# file; the Linux distribution packages ship only the library and a pkg-config file).
# build.sh installs this file into <prefix>/lib/cmake/lz4 so that
# find_package(lz4 CONFIG), which mo2-bsatk's own config also performs, works.
if (TARGET lz4::lz4)
  return()
endif()

find_path(lz4_INCLUDE_DIR NAMES lz4.h lz4frame.h)
find_library(lz4_LIBRARY NAMES lz4)
mark_as_advanced(lz4_INCLUDE_DIR lz4_LIBRARY)

if (NOT lz4_INCLUDE_DIR OR NOT lz4_LIBRARY)
  set(lz4_FOUND FALSE)
  if (lz4_FIND_REQUIRED)
    message(FATAL_ERROR "lz4 not found (install the lz4 development package)")
  endif()
  return()
endif()

add_library(lz4::lz4 UNKNOWN IMPORTED)
set_target_properties(lz4::lz4 PROPERTIES
  IMPORTED_LOCATION "${lz4_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${lz4_INCLUDE_DIR}")
set(lz4_FOUND TRUE)
