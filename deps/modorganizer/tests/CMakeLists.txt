cmake_minimum_required(VERSION 3.16)

# organizer_tests links the data tree, the Linux env layer, the file tree of archives
# and the Linux parts of running programs (organizer_datatree, organizer_env,
# organizer_archivetree and organizer_launch, see src/CMakeLists.txt), not the whole
# application
add_executable(organizer_tests)
mo2_configure_tests(organizer_tests NO_MAIN NO_MOCK WARNINGS 4)

# mo2_configure_tests takes every .cpp of this directory; bench_refresh.cpp is the
# refresh benchmark (organizer_bench_refresh, see the top-level CMakeLists.txt), with a
# main() of its own
get_target_property(_organizer_tests_sources organizer_tests SOURCES)
list(FILTER _organizer_tests_sources EXCLUDE REGEX "(^|/)bench_refresh\\.cpp$")
set_property(TARGET organizer_tests PROPERTY SOURCES ${_organizer_tests_sources})

target_link_libraries(organizer_tests PRIVATE
	organizer_launch organizer_archivetree organizer_env organizer_datatree)

# the shared/ headers rely on the precompiled header of the application
target_precompile_headers(organizer_tests REUSE_FROM organizer_datatree)

# the archive tests load the 7z.so of the 7zip package (see test_archivefiletree.cpp)
find_package(7zip CONFIG REQUIRED)
target_compile_definitions(organizer_tests PRIVATE MO2_TESTS_7Z="${7zip_LIBRARY}")
