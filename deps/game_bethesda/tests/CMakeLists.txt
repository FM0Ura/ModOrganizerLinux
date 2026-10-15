cmake_minimum_required(VERSION 3.16)

find_package(GTest REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)

# Skyrim SE: loads the plugin like Mod Organizer does (QPluginLoader)
add_executable(game_bethesda_tests test_skyrimse_linux.cpp)
mo2_configure_tests(game_bethesda_tests NO_SOURCES NO_MAIN WARNINGS OFF)
target_compile_definitions(game_bethesda_tests
	PRIVATE SKYRIMSE_PLUGIN="$<TARGET_FILE:game_skyrimse>")
target_link_libraries(game_bethesda_tests PRIVATE mo2::uibase)
add_dependencies(game_bethesda_tests game_skyrimse)

# the save reader of the static library game_gamebryo, in its own executable: the
# game plugins loaded by game_bethesda_tests have their own copy of game_gamebryo
add_executable(game_gamebryo_tests test_savegame_reader_linux.cpp)
mo2_configure_tests(game_gamebryo_tests NO_SOURCES WARNINGS OFF)
target_link_libraries(game_gamebryo_tests PRIVATE game_gamebryo)
