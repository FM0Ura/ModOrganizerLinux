cmake_minimum_required(VERSION 3.22)

# Tests of the Linux bs_archive_auto (spec of Phase 4e, section 5.3), Linux only.

# distribution packages (e.g. Fedora's GTest) only export a NOCONFIG configuration
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel RelWithDebInfo Release None NOCONFIG)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel None NOCONFIG)
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo MinSizeRel None NOCONFIG)

find_package(GTest REQUIRED)

add_executable(bsa_packer_linux_tests bs_archive_auto_tests.cpp)
set_target_properties(bsa_packer_linux_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_link_libraries(bsa_packer_linux_tests PRIVATE bsa_packer GTest::gtest GTest::gtest_main)
add_test(NAME bsa_packer_linux_tests COMMAND bsa_packer_linux_tests)
