cmake_minimum_required(VERSION 3.16)

# organizer_tests links the data tree, the Linux env layer, the file tree of archives
# and the Linux parts of running programs (organizer_datatree, organizer_env,
# organizer_archivetree and organizer_launch, see src/CMakeLists.txt), not the whole
# application
add_executable(organizer_tests)
mo2_configure_tests(organizer_tests NO_MAIN NO_MOCK WARNINGS 4)
target_link_libraries(organizer_tests PRIVATE
	organizer_launch organizer_archivetree organizer_env organizer_datatree)

# the shared/ headers rely on the precompiled header of the application
target_precompile_headers(organizer_tests REUSE_FROM organizer_datatree)

# the archive tests load the 7z.so of the 7zip package (see test_archivefiletree.cpp)
find_package(7zip CONFIG REQUIRED)
target_compile_definitions(organizer_tests PRIVATE MO2_TESTS_7Z="${7zip_LIBRARY}")
