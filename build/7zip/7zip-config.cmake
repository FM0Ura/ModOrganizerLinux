# CMake package for 7-Zip 26.01 as installed by build/7zip/build-7zip.sh.
#
#   find_package(7zip CONFIG REQUIRED)
#   target_link_libraries(foo PRIVATE 7zip::7zip)
#
# 7zip::7zip is an INTERFACE target that only carries the include directory
# (<prefix>/include/7zip/CPP, so "7zip/Archive/IArchive.h" and "Common/MyCom.h"
# resolve; the C/ tree is reached through the headers' relative includes). The
# library itself is loaded at run time with dlopen; its location is in
# 7zip_LIBRARY and in the 7zip_LIBRARY property of the target.
#
# 7zip_MYWINDOWS_SOURCE is 7-Zip's CPP/Common/MyWindows.cpp, which implements the
# Win32/COM functions (SysAllocString, VariantClear, GetLastError, ...) declared by
# Common/MyWindows.h and not exported by 7z.so. A consumer using the COM interfaces
# compiles it into its own library; it includes "StdAfx.h", found in
# ${7zip_INCLUDE_DIR}/Common.

get_filename_component(_7zip_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(7zip_INCLUDE_DIR "${_7zip_prefix}/include/7zip/CPP")
set(7zip_LIBRARY "${_7zip_prefix}/lib/7z.so")
set(7zip_MYWINDOWS_SOURCE "${_7zip_prefix}/share/7zip/src/MyWindows.cpp")
set(7zip_LICENSE_FILE "${_7zip_prefix}/share/licenses/7zip/License.txt")

foreach(_7zip_path IN ITEMS "${7zip_INCLUDE_DIR}" "${7zip_LIBRARY}" "${7zip_MYWINDOWS_SOURCE}")
    if(NOT EXISTS "${_7zip_path}")
        set(7zip_FOUND FALSE)
        set(7zip_NOT_FOUND_MESSAGE "7zip package is incomplete: missing ${_7zip_path}")
        return()
    endif()
endforeach()

if(NOT TARGET 7zip::7zip)
    add_library(7zip::7zip INTERFACE IMPORTED)
    set_target_properties(7zip::7zip PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${7zip_INCLUDE_DIR}"
        7zip_LIBRARY "${7zip_LIBRARY}")
endif()

unset(_7zip_prefix)
unset(_7zip_path)
