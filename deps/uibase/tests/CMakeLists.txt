cmake_minimum_required(VERSION 3.16)

add_executable(uibase-tests EXCLUDE_FROM_ALL)
target_sources(uibase-tests
	PRIVATE
		test_main.cpp
		test_formatters.cpp
		test_ifiletree.cpp
		test_strings.cpp
		test_versioning.cpp
)
if (NOT WIN32)
	# Linux implementations of the Win32-backed functions (src/linux)
	target_sources(uibase-tests PRIVATE test_linux_utility.cpp test_linux_filetime.cpp
		test_linux_inifile.cpp
		test_linux_vdf.cpp)
	target_compile_definitions(uibase-tests
		PRIVATE UIBASE_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
	# tests only exist with BUILD_TESTING=ON (build.sh --tests): build them with the
	# library so that ctest can run them
	set_target_properties(uibase-tests PROPERTIES EXCLUDE_FROM_ALL OFF)
endif()
mo2_configure_tests(uibase-tests NO_SOURCES NO_MAIN NO_MOCK WARNINGS 4 AUTOMOC OFF)
target_link_libraries(uibase-tests PRIVATE uibase)
