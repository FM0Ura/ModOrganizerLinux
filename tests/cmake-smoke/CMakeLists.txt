# Smoke test for deps/cmake_common on Linux (GCC): consumes the installed modules
# through find_package(mo2-cmake) exactly like the other MO2 projects do.
cmake_minimum_required(VERSION 3.22)

project(mo2_cmake_smoke LANGUAGES CXX)

find_package(mo2-cmake CONFIG REQUIRED)

# shared library: exercises C++23, warnings, PIC, hidden visibility, AUTOMOC and
# the translation (lupdate/lrelease) steps of mo2_configure_target
add_subdirectory(lib)

# executable: uses std::expected / std::format and checks the library's symbol
# visibility at runtime; prints "ok" on success
add_executable(smoke_main main.cpp)
mo2_configure_target(smoke_main NO_SOURCES AUTOMOC OFF TRANSLATIONS OFF WARNINGS ON)
target_link_libraries(smoke_main PRIVATE smoke ${CMAKE_DL_LIBS})

# unit tests: exercises mo2_configure_tests (only with BUILD_TESTING, i.e. build.sh --tests)
include(CTest)
if (BUILD_TESTING)
	add_subdirectory(unit)
endif()

# Python (Phase 4d.1): on Linux, cmake_common uses the system's Python and pyuic6
if (NOT WIN32)
	add_custom_target(smoke_python ALL)
	mo2_python_uifiles(smoke_python FILES ${CMAKE_CURRENT_SOURCE_DIR}/python/form.ui)
	find_package(Python REQUIRED COMPONENTS Interpreter)
	if (NOT MO2_PYTHON_VERSION STREQUAL "${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}")
		message(FATAL_ERROR "MO2_PYTHON_VERSION is ${MO2_PYTHON_VERSION}, the system has "
			"${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}")
	endif()
endif()
