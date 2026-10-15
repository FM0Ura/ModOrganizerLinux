cmake_minimum_required(VERSION 3.16)

# libbsarch is only available on Windows; without it, files are read from BSAs with
# bsatk (see organizercore.cpp)
if (WIN32)
	set(_mo2_with_libbsarch_default ON)
else()
	set(_mo2_with_libbsarch_default OFF)
endif()
option(MO2_WITH_LIBBSARCH "use libbsarch to read files from BSA archives"
	${_mo2_with_libbsarch_default})

find_package(mo2-cmake CONFIG REQUIRED)

if (WIN32)
	find_package(usvfs CONFIG REQUIRED)
endif()

if (NOT WIN32)
	# replaced by -idirafter below
	set(MO2_CMAKE_DEPRECATED_UIBASE_INCLUDE OFF)
endif()

find_package(mo2-uibase CONFIG REQUIRED)

# the sources include the uibase headers without their uibase/ prefix (<log.h>, ...),
# see MO2_CMAKE_DEPRECATED_UIBASE_INCLUDE in the top-level CMakeLists.txt; on Linux, the
# directories are searched after the system ones: uibase's strings.h would hide the
# system <strings.h>, which glibc's <string.h> includes in an extern "C" block (the
# MOBase functions of uibase's strings.h would then get C linkage); moc only takes -I
set(_organizer_uibase_include_options)
set(_organizer_uibase_moc_options)
if (NOT WIN32)
	# the same directories as the mo2-uibase package adds with
	# MO2_CMAKE_DEPRECATED_UIBASE_INCLUDE (<prefix>/lib/cmake/mo2-uibase/../../..)
	get_filename_component(_uibase_prefix "${mo2-uibase_DIR}/../../.." ABSOLUTE)
	foreach(_dir uibase uibase/game_features)
		list(APPEND _organizer_uibase_include_options
			"SHELL:-idirafter ${_uibase_prefix}/include/${_dir}")
		list(APPEND _organizer_uibase_moc_options "-I${_uibase_prefix}/include/${_dir}")
	endforeach()
endif()
find_package(mo2-archive CONFIG REQUIRED)
find_package(mo2-lootcli-header CONFIG REQUIRED)
find_package(mo2-bsatk CONFIG REQUIRED)
find_package(mo2-esptk CONFIG REQUIRED)
find_package(mo2-dds-header CONFIG REQUIRED)
if (MO2_WITH_LIBBSARCH)
	find_package(mo2-libbsarch CONFIG REQUIRED)
endif()

find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets WebSockets NetworkAuth)
if (WIN32)
	find_package(Boost CONFIG REQUIRED COMPONENTS program_options thread interprocess signals2 uuid accumulators)
	set(_organizer_boost
		Boost::program_options Boost::signals2 Boost::uuid Boost::accumulators)
else()
	# interprocess, signals2, uuid and accumulators are header-only and some
	# distributions (Fedora) install no CMake package for them
	find_package(Boost CONFIG REQUIRED COMPONENTS program_options thread)
	set(_organizer_boost Boost::program_options Boost::thread Boost::headers)
endif()
find_package(7zip CONFIG REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(organizer)
set_target_properties(organizer PROPERTIES
	OUTPUT_NAME "ModOrganizer"
	WIN32_EXECUTABLE TRUE)

# disable translations because we want to be able to install somewhere else if
# required
mo2_configure_target(organizer WARNINGS 4 TRANSLATIONS OFF)

# mo2_configure_target() adds every source file under src/; keep the platform-specific
# ones for the current platform only:
# - Windows: everything except src/linux/,
# - Linux: src/linux/<name>_linux.cpp replace the Windows-only files below, resource
#   scripts and manifests are Windows-only; the other env*.cpp are built on both
#   platforms, with their Win32 functions in #ifdef _WIN32 blocks, and
#   src/linux/env*_linux.cpp implement those functions on Linux.
#
# On Linux, the data tree (shared/ file register) and the filesystem part of env are
# built as a static library, organizer_datatree, and the rest of env, except env.cpp
# (which needs the settings), as organizer_env, the file tree of archives used by
# the installation as organizer_archivetree, and the Linux parts of running programs
# (the virtual file system connector, spawn and the availability of LOOT) as
# organizer_launch; the application links them and so do the unit tests (tests/),
# without the rest of the application.
set(_organizer_datatree_sources
	shared/directoryentry.cpp
	shared/fileentry.cpp
	shared/fileregister.cpp
	shared/filesorigin.cpp
	shared/originconnection.cpp
	linux/envfs_linux.cpp
	linux/util_linux.cpp)

set(_organizer_env_sources
	envmetrics.cpp
	envmodule.cpp
	envsecurity.cpp
	envshell.cpp
	envshortcut.cpp
	envwindows.cpp
	linux/envdump_linux.cpp
	linux/envmetrics_linux.cpp
	linux/envmodule_linux.cpp
	linux/envsecurity_linux.cpp
	linux/envshell_linux.cpp
	linux/envshortcut_linux.cpp
	linux/envwindows_linux.cpp)

set(_organizer_launch_sources
	linux/loot_linux.cpp
	linux/spawn_linux.cpp
	linux/vfsconnector_linux.cpp)

get_target_property(_organizer_sources organizer SOURCES)
set(_organizer_platform_sources)
foreach(_source IN LISTS _organizer_sources)
	file(RELATIVE_PATH _relative ${CMAKE_CURRENT_SOURCE_DIR} ${_source})
	if (WIN32)
		if (_relative MATCHES "^linux/")
			continue()
		endif()
	else()
		if (_relative MATCHES "^(envfs|spawn|usvfsconnector|shared/windows_error|shared/util)\\.cpp$"
				OR _relative MATCHES "\\.(rc|manifest)$"
				OR _relative IN_LIST _organizer_datatree_sources
				OR _relative IN_LIST _organizer_env_sources
				OR _relative IN_LIST _organizer_launch_sources
				OR _relative STREQUAL "archivefiletree.cpp")
			continue()
		endif()
	endif()
	list(APPEND _organizer_platform_sources ${_source})
endforeach()
set_property(TARGET organizer PROPERTY SOURCES ${_organizer_platform_sources})

if (NOT WIN32)
	add_library(organizer_datatree STATIC ${_organizer_datatree_sources})
	mo2_configure_target(organizer_datatree
		NO_SOURCES WARNINGS 4 TRANSLATIONS OFF AUTOMOC OFF)
	target_precompile_headers(organizer_datatree PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.h)
	target_include_directories(organizer_datatree PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
	# public: the headers of the data tree need them, and so does the precompiled
	# header that the tests reuse
	target_link_libraries(organizer_datatree PUBLIC
		mo2::uibase mo2::bsatk ${_organizer_boost}
		Qt6::WebEngineWidgets Qt6::WebSockets Qt6::NetworkAuth)
	target_compile_options(organizer_datatree PUBLIC ${_organizer_uibase_include_options})

	add_library(organizer_env STATIC ${_organizer_env_sources})
	mo2_configure_target(organizer_env
		NO_SOURCES WARNINGS 4 TRANSLATIONS OFF AUTOMOC OFF)
	target_precompile_headers(organizer_env REUSE_FROM organizer_datatree)
	target_link_libraries(organizer_env PUBLIC organizer_datatree)

	add_library(organizer_archivetree STATIC archivefiletree.cpp)
	mo2_configure_target(organizer_archivetree
		NO_SOURCES WARNINGS 4 TRANSLATIONS OFF AUTOMOC OFF)
	target_precompile_headers(organizer_archivetree REUSE_FROM organizer_datatree)
	target_link_libraries(organizer_archivetree PUBLIC organizer_datatree mo2::archive)

	add_library(organizer_launch STATIC ${_organizer_launch_sources})
	mo2_configure_target(organizer_launch
		NO_SOURCES WARNINGS 4 TRANSLATIONS OFF AUTOMOC OFF)
	target_precompile_headers(organizer_launch REUSE_FROM organizer_datatree)
	target_link_libraries(organizer_launch PUBLIC organizer_env mo2::lootcli-header)

	target_link_libraries(organizer PRIVATE
		organizer_launch organizer_archivetree organizer_env organizer_datatree)
endif()

# we add translations "manually" to handle MO2_INSTALL_IS_BIN
mo2_add_translations(organizer
	INSTALL_RELEASE
	INSTALL_DIRECTORY "${_bin}/translations"
	SOURCES ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
	mo2_set_project_to_run_from_install(
		organizer EXECUTABLE ${CMAKE_INSTALL_PREFIX}/${_bin}/ModOrganizer.exe)

	target_link_libraries(organizer PRIVATE
		Shlwapi Bcrypt usvfs::usvfs Version Dbghelp)
else()
	# MSVC finds "header.h" included from the generated ui_*.h files in the directory of
	# the source that includes them, GCC only through the include path
	target_include_directories(organizer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_options(organizer PRIVATE ${_organizer_uibase_include_options})
	set_property(TARGET organizer APPEND PROPERTY
		AUTOMOC_MOC_OPTIONS ${_organizer_uibase_moc_options})
	target_link_libraries(organizer PRIVATE ${CMAKE_DL_LIBS})
endif()

target_link_libraries(organizer PRIVATE
	mo2::uibase mo2::archive
	mo2::bsatk mo2::esptk mo2::lootcli-header
	${_organizer_boost}
	Qt6::WebEngineWidgets Qt6::WebSockets Qt6::NetworkAuth)

if (MO2_WITH_LIBBSARCH)
	target_link_libraries(organizer PRIVATE mo2::libbsarch)
	target_compile_definitions(organizer PRIVATE MO2_WITH_LIBBSARCH)
endif()

if (WIN32)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/dlls.manifest.qt6"
			DESTINATION ${_bin}/dlls
			CONFIGURATIONS Release RelWithDebInfo
			RENAME dlls.manifest)
	install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/dlls.manifest.debug.qt6"
			DESTINATION ${_bin}/dlls
			CONFIGURATIONS Debug
			RENAME dlls.manifest)
endif()

# stylesheets, tutorials and resources are next to the executable on every platform
if (NOT MO2_SKIP_STYLESHEETS_INSTALL)
	install(
		DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/stylesheets"
		DESTINATION ${_bin})
endif()

if (NOT MO2_SKIP_TUTORIALS_INSTALL)
	install(
		DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tutorials"
		DESTINATION ${_bin})
endif()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/markdown.html"
	DESTINATION ${_bin}/resources)

if (WIN32)
	# install ModOrganizer.exe itself
	install(FILES $<TARGET_FILE:organizer> DESTINATION ${_bin})

	# install dependencies DLLs
	if (MO2_WITH_LIBBSARCH)
		install(FILES $<TARGET_FILE:mo2::libbsarch> DESTINATION ${_bin}/dlls)
	endif()
	install(FILES $<TARGET_FILE:7zip::7zip> DESTINATION ${_bin}/dlls)

	# this may copy over the ones from uibase/usvfs
	# - when building with mob, this should not matter as the files should be identical
	# - when building standalone, this should help having matching USVFS DLL between the
	#   build and the installation
	# - this may cause issue with uibase in standalone mode if the installed version does
	#   not match the one used for the build, but there would be other issue anyway (e.g.
	#   different uibase.dll between modorganizer and plugins)
	#
	install(FILES
		$<TARGET_FILE:mo2::uibase>
		$<TARGET_FILE:usvfs_x64::usvfs_dll>
		$<TARGET_FILE:usvfs_x86::usvfs_dll>
		$<TARGET_FILE:usvfs_x64::usvfs_proxy>
		$<TARGET_FILE:usvfs_x86::usvfs_proxy>
	DESTINATION ${_bin})

	# do not install PDB if CMAKE_INSTALL_PREFIX is "bin"
	if (NOT MO2_INSTALL_IS_BIN)
		install(FILES $<TARGET_PDB_FILE:organizer> DESTINATION pdb)
	endif()

	mo2_deploy_qt(
		DIRECTORY ${_bin}
		BINARIES ModOrganizer.exe $<TARGET_FILE_NAME:mo2::uibase>)
else()
	# the shared libraries (libuibase.so, libmo2-archive.so, 7z.so, ...) are in lib/,
	# next to bin/
	set_target_properties(organizer PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
	install(TARGETS organizer RUNTIME DESTINATION ${_bin})
endif()

# set source groups for VS
mo2_add_filter(NAME src/application GROUPS
	iuserinterface
	commandline
	main
	moapplication
	moshortcut
	multiprocess
	sanitychecks
	selfupdater
	systemtraymanager
	updatedialog
)

mo2_add_filter(NAME src/browser GROUPS
	browserdialog
	browserview
)

mo2_add_filter(NAME src/categories GROUPS
	categories
	categoriestable
	categoriesdialog
	categoryimportdialog
)

mo2_add_filter(NAME src/core GROUPS
	archivefiletree
	githubpp
	installationmanager
	nexusinterface
	nexusoauthlogin
	nexusoauthtokens
	nexusoauthconfig
	nxmaccessmanager
	organizercore
	game_features
	plugincontainer
	apiuseraccount
	processrunner
	qdirfiletree
	virtualfiletree
	uilocker
)

mo2_add_filter(NAME src/dialogs GROUPS
	aboutdialog
	activatemodsdialog
	credentialsdialog
	filedialogmemory
	forcedloaddialog
	forcedloaddialogwidget
	listdialog
	messagedialog
	motddialog
	overwriteinfodialog
	queryoverwritedialog
	problemsdialog
	savetextasdialog
	selectiondialog
	syncoverwritedialog
	transfersavesdialog
	waitingonclosedialog
)

mo2_add_filter(NAME src/downloads GROUPS
	downloadlist
	downloadlistview
	downloadmanager
)

mo2_add_filter(NAME src/env GROUPS
	env
	envdump
	envfs
	envmetrics
	envmodule
	envsecurity
	envshell
	envshortcut
	envwindows
)

mo2_add_filter(NAME src/executables GROUPS
	executableslist
	editexecutablesdialog
)

mo2_add_filter(NAME src/instances GROUPS
	createinstancedialog
	createinstancedialogpages
	instancemanager
	instancemanagerdialog
)

mo2_add_filter(NAME src/loot GROUPS
	loot
	lootdialog
)

mo2_add_filter(NAME src/mainwindow GROUPS
	datatab
	downloadstab
	iconfetcher
	filetree
	filetreeitem
	filetreemodel
	mainwindow
	savestab
	statusbar
)

mo2_add_filter(NAME src/modinfo GROUPS
	modinfo
	modinfobackup
	modinfoforeign
	modinfooverwrite
	modinforegular
	modinfoseparator
	modinfowithconflictinfo
)

mo2_add_filter(NAME src/modinfo/dialog GROUPS
	modinfodialog
	modinfodialogcategories
	modinfodialogconflicts
	modinfodialogconflictsmodels
	modinfodialogesps
	modinfodialogfiletree
	modinfodialogfwd
	modinfodialogimages
	modinfodialognexus
	modinfodialogtab
	modinfodialogtextfiles
)

mo2_add_filter(NAME src/modinfo/dialog/widgets GROUPS
	modidlineedit
)

mo2_add_filter(NAME src/modlist GROUPS
	modlist
	modlistdropinfo
	modlistsortproxy
	modlistbypriorityproxy
)

mo2_add_filter(NAME src/modlist/view GROUPS
	filterlist
	modlistview
	modlistviewactions
	modlistcontextmenu
	modflagicondelegate
	modcontenticondelegate
	modconflicticondelegate
	modlistversiondelegate
)

mo2_add_filter(NAME src/plugins GROUPS
	pluginlist
	pluginlistsortproxy
	pluginlistview
	pluginlistcontextmenu
)

mo2_add_filter(NAME src/previews GROUPS
	previewdialog
	previewgenerator
)

mo2_add_filter(NAME src/profiles GROUPS
	profile
	profileinputdialog
	profilesdialog
)

mo2_add_filter(NAME src/proxies GROUPS
	downloadmanagerproxy
	executableslistproxy
	gamefeaturesproxy
	instancemanagerproxy
	modlistproxy
	organizerproxy
	pluginlistproxy
	proxyutils
)

mo2_add_filter(NAME src/register GROUPS
	shared/directoryentry
	shared/fileentry
	shared/filesorigin
	shared/fileregister
	shared/fileregisterfwd
	shared/originconnection
	directoryrefresher
)

mo2_add_filter(NAME src/settings GROUPS
	settings
	settingsutilities
)

mo2_add_filter(NAME src/settingsdialog GROUPS
	settingsdialog
	settingsdialogdiagnostics
	settingsdialoggeneral
	settingsdialognexus
	settingsdialogpaths
	settingsdialogplugins
	settingsdialogworkarounds
	settingsdialogmodlist
	settingsdialogtheme
	disableproxyplugindialog
)

mo2_add_filter(NAME src/utilities GROUPS
	shared/appconfig
	bbcode
	csvbuilder
	persistentcookiejar
	serverinfo
	spawn
	shared/util
	usvfsconnector
	shared/windows_error
	thread_utils
	json
	glob_matching
)

mo2_add_filter(NAME src/widgets GROUPS
	colortable
	genericicondelegate
	filerenamer
	icondelegate
	lcdnumber
	loglist
	loghighlighter
	noeditdelegate
	qtgroupingproxy
	texteditor
	viewmarkingscrollbar
	modelutils
	copyeventfilter
)
