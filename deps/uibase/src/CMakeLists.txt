cmake_minimum_required(VERSION 3.16)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Network Qml Quick QuickWidgets Widgets)
find_package(spdlog CONFIG REQUIRED)

set(root_headers
	../include/uibase/delayedfilewriter.h
	../include/uibase/diagnosisreport.h
	../include/uibase/dllimport.h
	../include/uibase/errorcodes.h
	../include/uibase/eventfilter.h
	../include/uibase/exceptions.h
	../include/uibase/executableinfo.h
	../include/uibase/filemapping.h
	../include/uibase/filesystemutilities.h
	../include/uibase/guessedvalue.h
	../include/uibase/idownloadmanager.h
	../include/uibase/json.h
	../include/uibase/log.h
	../include/uibase/memoizedlock.h
	../include/uibase/moassert.h
	../include/uibase/modrepositoryfileinfo.h
	../include/uibase/nxmurl.h
	../include/uibase/pluginrequirements.h
	../include/uibase/pluginsetting.h
	../include/uibase/registry.h
	../include/uibase/report.h
	../include/uibase/safewritefile.h
	../include/uibase/scopeguard.h
	../include/uibase/steamutility.h
	../include/uibase/strings.h
	../include/uibase/utility.h
	../include/uibase/versioning.h
	../include/uibase/versioninfo.h
)
if (NOT WIN32)
	# Win32 types and constants used by the public headers
	list(APPEND root_headers ../include/uibase/win32compat.h)
endif()
if (NOT WIN32)
	# Linux-only helpers (FILETIME, INI and VDF files)
	set(linux_headers
		../include/uibase/linux/filetime.h
		../include/uibase/linux/inifile.h
		../include/uibase/linux/steam.h
		../include/uibase/linux/vdf.h
	)
endif()
set(interface_headers
    ../include/uibase/iexecutable.h
    ../include/uibase/iexecutableslist.h
	../include/uibase/ifiletree.h
	../include/uibase/ifiletree_utils.h
	../include/uibase/iinstallationmanager.h
	../include/uibase/iinstance.h
	../include/uibase/iinstancemanager.h
	../include/uibase/imodinterface.h
	../include/uibase/imodlist.h
	../include/uibase/imodrepositorybridge.h
	../include/uibase/imoinfo.h
	../include/uibase/iplugin.h
	../include/uibase/iplugindiagnose.h
	../include/uibase/ipluginfilemapper.h
	../include/uibase/iplugingame.h
	../include/uibase/iplugingamefeatures.h
	../include/uibase/iplugininstaller.h
	../include/uibase/iplugininstallercustom.h
	../include/uibase/iplugininstallersimple.h
	../include/uibase/ipluginlist.h
	../include/uibase/ipluginmodpage.h
	../include/uibase/ipluginpreview.h
	../include/uibase/ipluginproxy.h
	../include/uibase/iplugintool.h
	../include/uibase/iprofile.h
	../include/uibase/isavegame.h
	../include/uibase/isavegameinfowidget.h
)
set(tutorial_headers
	../include/uibase/tutorabledialog.h
	../include/uibase/tutorialcontrol.h
	../include/uibase/tutorialmanager.h
)
set(widget_headers
	../include/uibase/expanderwidget.h
	../include/uibase/filterwidget.h
	../include/uibase/finddialog.h
	../include/uibase/lineeditclear.h
	../include/uibase/linklabel.h
	../include/uibase/questionboxmemory.h
	../include/uibase/sortabletreewidget.h
	../include/uibase/taskprogressmanager.h
	../include/uibase/textviewer.h
	../include/uibase/widgetutility.h
)
set(game_features_header
	../include/uibase/game_features/bsainvalidation.h
	../include/uibase/game_features/dataarchives.h
	../include/uibase/game_features/game_feature.h
	../include/uibase/game_features/gameplugins.h
	../include/uibase/game_features/igamefeatures.h
	../include/uibase/game_features/localsavegames.h
	../include/uibase/game_features/moddatachecker.h
	../include/uibase/game_features/moddatacontent.h
	../include/uibase/game_features/savegameinfo.h
	../include/uibase/game_features/scriptextender.h
	../include/uibase/game_features/unmanagedmods.h
)
set(formatters_header
	../include/uibase/formatters/enums.h
	../include/uibase/formatters/qt.h
	../include/uibase/formatters/strings.h
	../include/uibase/formatters.h
)

add_library(uibase SHARED)
mo2_configure_target(uibase NO_SOURCES WARNINGS ON EXTERNAL_WARNINGS ON)
mo2_default_source_group()

mo2_target_sources(uibase
	FOLDER src
	PRIVATE
	${root_headers}
	delayedfilewriter.cpp
	diagnosisreport.cpp
	eventfilter.cpp
	executableinfo.cpp
	filesystemutilities.cpp
	guessedvalue.cpp
	json.cpp
	log.cpp
	modrepositoryfileinfo.cpp
	nxmurl.cpp
	pch.cpp
	pluginrequirements.cpp
	report.cpp
	safewritefile.cpp
	scopeguard.cpp
	strings.cpp
	utility.cpp
	versioning.cpp
	versioninfo.cpp
)

mo2_target_sources(uibase
	FOLDER src/interfaces
	PRIVATE
	${interface_headers}
	ifiletree.cpp
	imodrepositorybridge.cpp
	imoinfo.cpp
)

mo2_target_sources(uibase
	FOLDER src/tutorials
	PRIVATE
	${tutorial_headers}
	tutorabledialog.cpp
	tutorialcontrol.cpp
	tutorialmanager.cpp
)

mo2_target_sources(uibase
	FOLDER src/widgets
	PRIVATE
	${widget_headers}
	expanderwidget.cpp
	finddialog.cpp
	lineeditclear.cpp
	linklabel.cpp
	questionboxmemory.cpp
	sortabletreewidget.cpp
	taskprogressmanager.cpp
	textviewer.cpp
	widgetutility.cpp
	filterwidget.cpp
)

# Win32-only implementations; on other systems src/linux provides the same functions
# (and the Win32-only parts of shared files are compiled under _WIN32)
if (WIN32)
	mo2_target_sources(uibase
		FOLDER src
		PRIVATE
		errorcodes.cpp
		registry.cpp
		steamutility.cpp
	)
else()
	mo2_target_sources(uibase
		FOLDER src/linux
		PRIVATE
		linux/errorcodes_linux.cpp
		linux/inifile_linux.cpp
		linux/peversion_linux.cpp
		linux/peversion_linux.h
		linux/registry_linux.cpp
		linux/report_linux.cpp
		linux/shell_linux.cpp
		linux/steam_linux.cpp
		linux/steamutility_linux.cpp
		linux/taskprogressmanager_linux.cpp
		linux/trash_linux.cpp
		linux/trash_linux.h
		linux/utility_linux.cpp
		linux/vdf_linux.cpp
		linux/win32_private.h
	)
endif()

mo2_target_sources(uibase FOLDER src/formatters	PRIVATE ${formatters_header})
mo2_target_sources(uibase FOLDER src/game_features	PRIVATE ${game_features_header})

target_sources(uibase
	PRIVATE
		finddialog.ui
		questionboxmemory.ui
		taskdialog.ui
		textviewer.ui

		${root_headers}
		pch.h

	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
		FILES
			${root_headers}
			${interface_headers}
			${tutorial_headers}
			${widget_headers}
			${game_features_header}
			${formatters_header}
			${linux_headers}
)

set_target_properties(uibase PROPERTIES DEBUG_POSTFIX d)

if (WIN32)
	target_sources(uibase PRIVATE version.rc)
endif()

# TODO: remove this after fixing UIBase #include<> directives
if (WIN32)
	target_include_directories(uibase PRIVATE
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/uibase>
		$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/uibase/game_features>
	)
else()
	# for "..." includes only: as -I directories, uibase's strings.h would replace the
	# system <strings.h> (included by <string.h>)
	target_compile_options(uibase PRIVATE
		"-iquote${PROJECT_SOURCE_DIR}/include/uibase"
		"-iquote${PROJECT_SOURCE_DIR}/include/uibase/game_features"
	)
endif()

add_library(mo2::uibase ALIAS uibase)

target_compile_definitions(uibase PRIVATE -DUIBASE_EXPORT SPDLOG_USE_STD_FORMAT)

target_link_libraries(uibase
	PUBLIC Qt6::Widgets Qt6::Network Qt6::QuickWidgets
	PRIVATE spdlog::spdlog_header_only Qt6::Qml Qt6::Quick)

if (WIN32)
	target_link_libraries(uibase PRIVATE Version)
else()
	# pch.h and some headers use MSVC's #pragma warning
	target_compile_options(uibase PRIVATE -Wno-unknown-pragmas)
endif()

# installation
install(TARGETS uibase EXPORT uibaseTargets FILE_SET HEADERS)
if (WIN32)
	install(FILES $<TARGET_PDB_FILE:uibase> DESTINATION pdb)
endif()
install(EXPORT uibaseTargets
	FILE mo2-uibase-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-uibase
)

# MO2_CMAKE_DEPRECATED_UIBASE_INCLUDE (see cmake/config.cmake.in) lets consumers include
# the uibase headers without their uibase/ prefix. On Linux, include/uibase cannot be an
# include directory of the consumers: its strings.h would replace the system <strings.h>
# (included by <string.h>). The package installs instead include/uibase-deprecated, one
# forwarding header per public header except strings.h (the uibase headers that include
# it find it next to them), also for the headers of game_features at its top level.
if (NOT WIN32)
	set(_uibase_forward_dir ${CMAKE_CURRENT_BINARY_DIR}/uibase-deprecated)
	file(REMOVE_RECURSE ${_uibase_forward_dir})
	foreach(_header IN LISTS root_headers interface_headers tutorial_headers
			widget_headers game_features_header formatters_header)
		file(RELATIVE_PATH _rel ${PROJECT_SOURCE_DIR}/include/uibase
			${CMAKE_CURRENT_LIST_DIR}/${_header})
		if (_rel STREQUAL "strings.h")
			continue()
		endif()
		set(_forwards ${_rel})
		if (_rel MATCHES "^game_features/(.*)$")
			list(APPEND _forwards ${CMAKE_MATCH_1})
		endif()
		foreach(_forward IN LISTS _forwards)
			# from include/uibase-deprecated/<forward> to include/uibase/<rel>
			string(REGEX REPLACE "[^/]+" ".." _up ${_forward})
			string(REGEX REPLACE "\\.\\.$" "" _up ${_up})
			file(WRITE ${_uibase_forward_dir}/${_forward}
				"// generated by uibase: forwards to the header of the uibase package\n"
				"#include \"../${_up}uibase/${_rel}\"\n")
		endforeach()
	endforeach()
	install(DIRECTORY ${_uibase_forward_dir}/ DESTINATION include/uibase-deprecated)
endif()
