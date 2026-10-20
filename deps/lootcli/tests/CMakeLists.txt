# Linux: tests of posix_support.cpp (the LOOT data folder and the masterlist download)
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(lootcli_tests test_posix_support.cpp)
set_target_properties(lootcli_tests PROPERTIES CXX_STANDARD 20)
target_link_libraries(lootcli_tests PRIVATE lootcli_posix GTest::gtest_main)
gtest_discover_tests(lootcli_tests)
