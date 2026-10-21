cmake_minimum_required(VERSION 3.16)

find_package(mo2-uibase CONFIG REQUIRED)

set(PLUGIN_NAME "plugin_python")

add_library(proxy SHARED proxypython.cpp proxypython.h)
mo2_configure_plugin(proxy
    NO_SOURCES
    WARNINGS 4
    EXTERNAL_WARNINGS 4
    TRANSLATIONS OFF
    EXTRA_TRANSLATIONS
    ${CMAKE_CURRENT_SOURCE_DIR}/../runner
    ${CMAKE_CURRENT_SOURCE_DIR}/../mobase
    ${CMAKE_CURRENT_SOURCE_DIR}/../pybind11-qt)
mo2_default_source_group()
target_link_libraries(proxy PRIVATE runner mo2::uibase)
set_target_properties(proxy PROPERTIES OUTPUT_NAME ${PLUGIN_NAME})
if (WIN32)
	mo2_install_plugin(proxy FOLDER)
else()
	# Linux: the proxy is bin/plugins/plugin_python.so, next to the plugin_python folder
	set_target_properties(proxy PROPERTIES PREFIX "")
	mo2_install_plugin(proxy)
endif()

set(PLUGIN_PYTHON_DIR bin/plugins/${PLUGIN_NAME})

# install mobase
install(TARGETS mobase DESTINATION ${PLUGIN_PYTHON_DIR}/libs)

# install runner
if (WIN32)
	target_link_options(proxy PRIVATE "/DELAYLOAD:runner.dll")
	install(FILES $<TARGET_FILE:runner> DESTINATION ${PLUGIN_PYTHON_DIR}/dlls)
else()
	# Linux: the runner is found next to the proxy, in plugin_python/dlls; Python and
	# PyQt6 are the system's (spec of Phase 4d.1, section 3), nothing else is installed
	set_target_properties(proxy PROPERTIES
		INSTALL_RPATH "$ORIGIN/plugin_python/dlls"
		BUILD_WITH_INSTALL_RPATH OFF)
	# installed as a target, not as a file, so that its INSTALL_RPATH is applied
	install(TARGETS runner LIBRARY DESTINATION ${PLUGIN_PYTHON_DIR}/dlls)

	# the soname of the system's libpython, which the proxy opens by name: the libpython
	# file of the library is a link that only exists with the development package
	list(GET Python_LIBRARIES 0 _python_library)
	get_filename_component(_python_library "${_python_library}" REALPATH)
	get_filename_component(MO2_PYTHON_LIBRARY "${_python_library}" NAME)
	target_compile_definitions(proxy PRIVATE
		MO2_PYTHON_LIBRARY="${MO2_PYTHON_LIBRARY}"
		MO2_PYTHON_VERSION_STRING="${MO2_PYTHON_VERSION}")
endif()

# translations (custom location)
mo2_add_translations(proxy
    TS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../${PLUGIN_NAME}_en.ts
    SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../runner
    ${CMAKE_CURRENT_SOURCE_DIR}/../mobase
    ${CMAKE_CURRENT_SOURCE_DIR}/../pybind11-qt)

if (WIN32)
	# install DLLs files needed
	set(DLL_DIRS ${PLUGIN_PYTHON_DIR}/dlls)
	file(GLOB dlls_to_install
		${Python_HOME}/dlls/libffi*.dll
		${Python_HOME}/dlls/sqlite*.dll
		${Python_HOME}/dlls/libssl*.dll
		${Python_HOME}/dlls/libcrypto*.dll
		${Python_HOME}/python${Python_VERSION_MAJOR}*.dll)
	install(FILES ${dlls_to_install} DESTINATION ${DLL_DIRS})

	# install Python .pyd files
	set(PYLIB_DIR ${PLUGIN_PYTHON_DIR}/libs)
	file(GLOB libs_to_install ${Python_DLL_DIR}/*.pyd)
	install(FILES ${libs_to_install} DESTINATION ${PYLIB_DIR})

	# generate + install standard library
	set(pythoncore_zip "${CMAKE_CURRENT_BINARY_DIR}/pythoncore.zip")
	add_custom_command(
	    TARGET proxy POST_BUILD
	    COMMAND ${Python_EXECUTABLE}
	        "${CMAKE_CURRENT_SOURCE_DIR}\\build_pythoncore.py"
	        ${pythoncore_zip}
	    )
	install(FILES ${pythoncore_zip} DESTINATION ${PYLIB_DIR})

	# install PyQt6
	install(
	    DIRECTORY ${CMAKE_BINARY_DIR}/pylibs/PyQt${MO2_QT_VERSION_MAJOR}
	    DESTINATION ${PYLIB_DIR}
	    PATTERN "*.pyd"
	    PATTERN "*.pyi"
	    PATTERN "__pycache__" EXCLUDE
	    PATTERN "bindings" EXCLUDE
	    PATTERN "lupdate" EXCLUDE
	    PATTERN "Qt6" EXCLUDE
	    PATTERN "uic" EXCLUDE
	)
endif()
