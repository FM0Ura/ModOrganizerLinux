cmake_minimum_required(VERSION 3.22)

# Tests of the Linux bs_archive_auto (spec of Phase 4e, section 5.3), Linux only.

find_package(GTest REQUIRED)
# distribution packages (e.g. Fedora's GTest) only export a NOCONFIG configuration. The
# mapping is set on the GTest targets: a global CMAKE_MAP_IMPORTED_CONFIG_* would also
# apply to lz4::lz4, whose IMPORTED_LOCATION is not configuration specific.
foreach(gtest_target GTest::gtest GTest::gtest_main)
  set_target_properties(${gtest_target} PROPERTIES
    MAP_IMPORTED_CONFIG_MINSIZEREL "MinSizeRel;RelWithDebInfo;Release;None;NOCONFIG"
    MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release;MinSizeRel;None;NOCONFIG"
    MAP_IMPORTED_CONFIG_RELEASE "Release;RelWithDebInfo;MinSizeRel;None;NOCONFIG")
endforeach()

find_package(mo2-bsatk CONFIG REQUIRED)

add_executable(bsa_packer_linux_tests bs_archive_auto_tests.cpp)
set_target_properties(bsa_packer_linux_tests PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_link_libraries(bsa_packer_linux_tests PRIVATE bsa_packer mo2::bsatk GTest::gtest GTest::gtest_main)
add_test(NAME bsa_packer_linux_tests COMMAND bsa_packer_linux_tests)
