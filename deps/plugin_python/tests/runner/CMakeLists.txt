cmake_minimum_required(VERSION 3.22)

# setting-up the tests for the runner is a bit complex because we need a tons of
# things

# first we configure the tests as with other tests
add_executable(runner-tests EXCLUDE_FROM_ALL)
mo2_default_source_group()
mo2_target_sources(runner-tests
	FOLDER src
	PRIVATE
		test_diagnose.cpp
		test_filemapper.cpp
		test_game.cpp
		test_installer.cpp
		test_iplugin.cpp
		test_lifetime.cpp
		test_syspath.cpp
)
mo2_target_sources(runner-tests
	FOLDER src/mocks
	PRIVATE
		../mocks/DummyFileTree.h
		../mocks/MockOrganizer.h
		../mocks/TestPythonPaths.h
)
mo2_target_sources(runner-tests
	FOLDER src/plugins
	PRIVATE
		plugins/dummy-diagnose.py
		plugins/dummy-filemapper.py
		plugins/dummy-game.py
		plugins/dummy-installer.py
		plugins/dummy-iplugin.py
)
mo2_configure_tests(runner-tests NO_SOURCES WARNINGS 4)

set_target_properties(runner-tests PROPERTIES FOLDER tests/runner)

# link to runner
target_link_libraries(runner-tests PUBLIC runner)

# linking to Python - this is not required to get proper linking but required so that
# CMake generator variables will lookup appropriate DLLs for Python and update PATH
# accordingly thanks to mo2_configure_tests
target_link_libraries(runner-tests PUBLIC Python::Python)

# add mocks
target_include_directories(runner-tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../mocks)

set(PYLIB_DIR ${CMAKE_CURRENT_BINARY_DIR}/pylibs)
if (WIN32)
	mo2_python_pip_install(runner-tests
		DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/pylibs
		PACKAGES
			pytest
			PyQt${MO2_QT_VERSION_MAJOR}==${MO2_PYQT_VERSION}
			PyQt${MO2_QT_VERSION_MAJOR}-Qt${MO2_QT_VERSION_MAJOR}==${MO2_QT_VERSION})
endif()

add_dependencies(runner-tests mobase)

if (WIN32)
	set(PYTHONPATH "${PYLIB_DIR}\\;$<TARGET_FILE_DIR:mobase>\\;${Python_DLL_DIR}\\;${Python_LIB_DIR}")
else()
	# Linux: PyQt6 is the system's (spec of Phase 4d.1, section 1.2), the tests only
	# give mobase to the runner (TestPythonPaths.h)
	set(PYTHONPATH "$<TARGET_FILE_DIR:mobase>")
endif()

set_tests_properties(${runner-tests_gtests}
    PROPERTIES
    ENVIRONMENT "PLUGIN_DIR=${CMAKE_CURRENT_SOURCE_DIR}/plugins;PYTHONPATH=${PYTHONPATH}"
)

if (NOT WIN32)
	# Linux: the isolation of the Python of Mod Organizer from the user's packages
	# (~/.local, PYTHONPATH), in its own executable since Python is initialized once
	# per process and the test changes HOME and PYTHONPATH before (spec of Phase 4d.1,
	# section 5)
	add_executable(runner-isolation-tests EXCLUDE_FROM_ALL)
	mo2_target_sources(runner-isolation-tests
		FOLDER src
		PRIVATE
			test_isolation.cpp
	)
	mo2_target_sources(runner-isolation-tests
		FOLDER src/mocks
		PRIVATE
			../mocks/TestPythonPaths.h
	)
	mo2_configure_tests(runner-isolation-tests NO_SOURCES WARNINGS 4)
	set_target_properties(runner-isolation-tests PROPERTIES FOLDER tests/runner)
	target_link_libraries(runner-isolation-tests PUBLIC runner pybind11::embed)
	target_include_directories(runner-isolation-tests
		PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../mocks)
	target_compile_definitions(runner-isolation-tests
		PRIVATE MO2_PYTHON_VERSION_STRING="${MO2_PYTHON_VERSION}")
	add_dependencies(runner-isolation-tests mobase)
	set_tests_properties(${runner-isolation-tests_gtests}
		PROPERTIES
		ENVIRONMENT "PYTHONPATH=${PYTHONPATH}"
	)
endif()
