cmake_minimum_required(VERSION 3.16)

find_package(GTest REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)

# the tests load the plugins like Mod Organizer does (QPluginLoader); the sources of
# the tests are the files of this directory (mo2_configure_tests)
add_executable(game_bethesda_tests)
mo2_configure_tests(game_bethesda_tests NO_MAIN WARNINGS OFF)
target_compile_definitions(game_bethesda_tests
	PRIVATE SKYRIMSE_PLUGIN="$<TARGET_FILE:game_skyrimse>")
target_link_libraries(game_bethesda_tests PRIVATE mo2::uibase)
add_dependencies(game_bethesda_tests game_skyrimse)
