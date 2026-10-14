cmake_minimum_required(VERSION 3.16)

# organizer_tests links the data tree and the Linux env layer (organizer_datatree, see
# src/CMakeLists.txt), not the whole application
add_executable(organizer_tests)
mo2_configure_tests(organizer_tests NO_MAIN NO_MOCK WARNINGS 4)
target_link_libraries(organizer_tests PRIVATE organizer_datatree)

# the shared/ headers rely on the precompiled header of the application
target_precompile_headers(organizer_tests REUSE_FROM organizer_datatree)
