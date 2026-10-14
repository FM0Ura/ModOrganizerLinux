cmake_minimum_required(VERSION 3.16)

find_package(7zip CONFIG REQUIRED)

add_library(archive)

set_target_properties(archive PROPERTIES CXX_STANDARD 20)
target_link_libraries(archive PRIVATE 7zip::7zip)

target_sources(archive
	PRIVATE
		archive.cpp
		extractcallback.cpp
		extractcallback.h
		fileio.h
		formatter.h
		inputstream.cpp
		inputstream.h
		instrument.h
		interfaceguids.cpp
		library.h
		multioutputstream.cpp
		multioutputstream.h
		opencallback.cpp
		opencallback.h
		propertyvariant.cpp
		propertyvariant.h
		unknown_impl.h
	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
		FILES ${CMAKE_CURRENT_LIST_DIR}/../include/archive/archive.h
)

if (WIN32)
	target_sources(archive PRIVATE fileio.cpp version.rc)
else()
	# POSIX file IO, and the BSTR/VARIANT/GetLastError functions of 7-Zip's
	# MyWindows.cpp (not exported by 7z.so)
	target_sources(archive PRIVATE linux/fileio_linux.cpp linux/win32_compat.h
		linux/fileio_linux.h linux/utf.h ${7zip_MYWINDOWS_SOURCE})
	# MyWindows.cpp includes "StdAfx.h" from 7-Zip's Common/ directory
	set_property(SOURCE ${7zip_MYWINDOWS_SOURCE} APPEND PROPERTY
		INCLUDE_DIRECTORIES ${7zip_INCLUDE_DIR}/Common)
endif()

target_include_directories(archive PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include/archive)

if (MSVC)
	target_compile_options(archive
		PRIVATE
		"/MP"
		"/W4"
		"/external:anglebrackets"
		"/external:W0"
	)
	target_link_options(archive
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
	)

	set_target_properties(archive PROPERTIES VS_STARTUP_PROJECT archive)
endif()

if (NOT WIN32)
	target_link_libraries(archive PRIVATE ${CMAKE_DL_LIBS})
	target_compile_options(archive PRIVATE -fvisibility=hidden -Wall -Wextra)
	# do not clash with the system libarchive
	set_target_properties(archive PROPERTIES OUTPUT_NAME mo2-archive)
endif()

if (BUILD_STATIC)
	target_compile_definitions(archive PUBLIC -DMO2_ARCHIVE_BUILD_STATIC)
else()
	target_compile_definitions(archive PRIVATE -DMO2_ARCHIVE_BUILD_EXPORT)
endif()

add_library(mo2::archive ALIAS archive)

install(TARGETS archive EXPORT archiveTargets FILE_SET HEADERS)
if (NOT BUILD_STATIC AND WIN32)
	install(FILES $<TARGET_PDB_FILE:archive> DESTINATION pdb OPTIONAL)
endif()
install(EXPORT archiveTargets
	FILE mo2-archive-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-archive
)
