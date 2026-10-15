cmake_minimum_required(VERSION 3.16)

find_package(mo2-uibase CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_library(game_gamebryo STATIC
	dummybsa.cpp
	dummybsa.h
	gamebryobsainvalidation.cpp
	gamebryobsainvalidation.h
	gamebryodataarchives.cpp
	gamebryodataarchives.h
	gamebryogameplugins.cpp
	gamebryogameplugins.h
	gamebryolocalsavegames.cpp
	gamebryolocalsavegames.h
	gamebryomoddatachecker.cpp
	gamebryomoddatachecker.h
	gamebryomoddatacontent.cpp
	gamebryomoddatacontent.h
	gamebryosavegame.cpp
	gamebryosavegame.h
	gamebryosavegameinfo.cpp
	gamebryosavegameinfo.h
	gamebryosavegameinfowidget.cpp
	gamebryosavegameinfowidget.h
	gamebryosavegameinfowidget.ui
	gamebryoscriptextender.cpp
	gamebryoscriptextender.h
	gamebryounmanagedmods.cpp
	gamebryounmanagedmods.h
	gamegamebryo.cpp
	gamegamebryo.h
	vdf_parser.h
)
mo2_configure_target(game_gamebryo
	WARNINGS 4
	NO_SOURCES
	TRANSLATIONS ON
	EXTRA_TRANSLATIONS ${CMAKE_CURRENT_LIST_DIR}/../creation)
mo2_default_source_group()
target_link_libraries(game_gamebryo PUBLIC mo2::uibase PRIVATE ZLIB::ZLIB lz4::lz4 $<$<PLATFORM_ID:Windows>:Dbghelp>)
target_include_directories(game_gamebryo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Linux: the Win32 types and functions of the shared sources (linux/win32compat_linux.h,
# included before every source of the targets that link game_gamebryo) and the Linux
# implementations of the Win32 parts of GameGamebryo
if (NOT WIN32)
	target_sources(game_gamebryo PRIVATE
		linux/gamegamebryo_linux.cpp
		linux/gamebryosavegame_linux.h
		linux/win32compat_linux.cpp
		linux/win32compat_linux.h)
	target_compile_options(game_gamebryo PUBLIC
		"SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/linux/win32compat_linux.h")
endif()

install(FILES $<TARGET_FILE:game_gamebryo> DESTINATION lib)
