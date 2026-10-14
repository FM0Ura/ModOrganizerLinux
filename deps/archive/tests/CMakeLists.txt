cmake_minimum_required(VERSION 3.16)

# Tests for the archive library. They run against the 7z.so installed by the 7zip
# package and the fixtures in data/ (see data/make_fixtures.sh), so they are Linux
# only for now.
find_package(7zip CONFIG REQUIRED)

# distribution packages (e.g. Fedora's GTest) only export a NOCONFIG configuration
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel RelWithDebInfo Release None NOCONFIG)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel None NOCONFIG)
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo MinSizeRel None NOCONFIG)

find_package(GTest REQUIRED)

add_executable(archive_tests test_archive.cpp)
set_target_properties(archive_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(archive_tests PRIVATE archive GTest::gtest GTest::gtest_main)
target_compile_definitions(archive_tests PRIVATE
	MO2_ARCHIVE_TEST_DATA="${CMAKE_CURRENT_LIST_DIR}/data"
	MO2_ARCHIVE_TEST_7Z="${7zip_LIBRARY}")

add_test(NAME archive_tests COMMAND archive_tests)
