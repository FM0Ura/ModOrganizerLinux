cmake_minimum_required(VERSION 3.16)

# Linux: the LOOT data folder and the masterlist download of lootcli (posix_support.cpp,
# the Windows versions are in lootthread.cpp), a library of their own for the tests
if (NOT WIN32)
	find_package(CURL REQUIRED)
	add_library(lootcli_posix STATIC posix_support.cpp posix_support.h)
	set_target_properties(lootcli_posix PROPERTIES CXX_STANDARD 20)
	target_include_directories(lootcli_posix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
	target_link_libraries(lootcli_posix PUBLIC CURL::libcurl)
endif()

# Qt is not required, this allows us to skip building the executable and only create
# the single-header interface when using this as a VCPKG dependency

find_package(Qt6 CONFIG COMPONENTS Core)

if (Qt6_FOUND)

message(STATUS "Qt6 found, building lootcli executable")

find_package(tomlplusplus CONFIG REQUIRED)
find_package(libloot CONFIG REQUIRED)
find_package(Boost REQUIRED CONFIG COMPONENTS locale)

# avoid CMake error/warning
set_target_properties(libloot::libloot PROPERTIES
  MAP_IMPORTED_CONFIG_RELEASE RelWithDebInfo
  MAP_IMPORTED_CONFIG_MINSIZEREL RelWithDebInfo
)

add_executable(lootcli WIN32)
set_target_properties(lootcli PROPERTIES
	CXX_STANDARD 20
	WIN32_EXECUTABLE TRUE)
target_sources(lootcli
	PRIVATE
		game_settings.cpp
		game_settings.h
		lootthread.cpp
		lootthread.h
		main.cpp
		pch.h
		version.h
		${CMAKE_CURRENT_SOURCE_DIR}/../include/lootcli/lootcli.h
)
if (WIN32)
	target_sources(lootcli PRIVATE version.rc)
endif()
target_compile_definitions(lootcli
	PRIVATE
	_UNICODE UNICODE
	_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
target_include_directories(lootcli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_precompile_headers(lootcli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)
if (WIN32)
	target_link_libraries(lootcli
	    PRIVATE libloot::libloot Boost::headers Boost::locale
		tomlplusplus::tomlplusplus Qt6::Core)
else()
	# lootcli may run on the host (started through mo2-vfs from the mo2-run box), which
	# has no libtomlplusplus: toml++ is used header-only; the MSVC pragmas of pch.h are
	# not for GCC; the libloot.so next to it is found through $ORIGIN
	target_include_directories(lootcli PRIVATE
		$<TARGET_PROPERTY:tomlplusplus::tomlplusplus,INTERFACE_INCLUDE_DIRECTORIES>)
	target_compile_definitions(lootcli PRIVATE TOML_HEADER_ONLY=1)
	target_compile_options(lootcli PRIVATE -Wno-unknown-pragmas)
	# Boost::locale lists its own dependencies (boost_thread, boost_charconv, ...) as
	# direct libraries of lootcli: only what lootcli really uses may be NEEDED
	target_link_options(lootcli PRIVATE -Wl,--as-needed)
	target_link_libraries(lootcli
		PRIVATE libloot::libloot Boost::headers Boost::locale Qt6::Core lootcli_posix)
	set_target_properties(lootcli PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

if (MSVC)
	target_compile_options(lootcli
		PRIVATE
		"/MP"
		"/W4"
		"/external:anglebrackets"
		"/external:W0"
	)
	target_link_options(lootcli
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
	)
	target_compile_definitions(lootcli PRIVATE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)

	set_target_properties(lootcli PROPERTIES VS_STARTUP_PROJECT lootcli)
endif()

if (WIN32)
install(FILES
	$<TARGET_FILE:lootcli>
	$<TARGET_FILE:libloot::libloot>
DESTINATION bin/loot)
else()
# install(TARGETS) sets the run path; libloot with its soname link (libloot.so.0)
install(TARGETS lootcli RUNTIME DESTINATION bin/loot)
install(IMPORTED_RUNTIME_ARTIFACTS libloot::libloot LIBRARY DESTINATION bin/loot)
endif()

endif()

# library to make the header available
add_library(lootcli-header INTERFACE)
target_sources(lootcli-header INTERFACE
	FILE_SET HEADERS
	BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
	FILES ${CMAKE_CURRENT_LIST_DIR}/../include/lootcli/lootcli.h)
add_library(mo2::lootcli-header ALIAS lootcli-header)

install(TARGETS lootcli-header EXPORT lootcliHeaderTargets FILE_SET HEADERS)
install(EXPORT lootcliHeaderTargets
	FILE mo2-lootcli-header-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-lootcli-header
)
