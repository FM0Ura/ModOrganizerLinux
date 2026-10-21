cmake_minimum_required(VERSION 3.16)

find_package(GTest REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)

# Skyrim SE, and the Steam games that xEdit supports (test_steam_games_linux.cpp): loads
# the plugins like Mod Organizer does (QPluginLoader)
add_executable(game_bethesda_tests test_skyrimse_linux.cpp test_steam_games_linux.cpp)
mo2_configure_tests(game_bethesda_tests NO_SOURCES NO_MAIN WARNINGS OFF)
target_compile_definitions(game_bethesda_tests PRIVATE
	SKYRIMSE_PLUGIN="$<TARGET_FILE:game_skyrimse>"
	GAME_PLUGIN_SKYRIM="$<TARGET_FILE:game_skyrim>"
	GAME_PLUGIN_SKYRIMVR="$<TARGET_FILE:game_skyrimvr>"
	GAME_PLUGIN_ENDERAL="$<TARGET_FILE:game_enderal>"
	GAME_PLUGIN_ENDERALSE="$<TARGET_FILE:game_enderalse>"
	GAME_PLUGIN_OBLIVION="$<TARGET_FILE:game_oblivion>"
	GAME_PLUGIN_FALLOUT3="$<TARGET_FILE:game_fallout3>"
	GAME_PLUGIN_FALLOUTNV="$<TARGET_FILE:game_falloutnv>"
	GAME_PLUGIN_FALLOUT4="$<TARGET_FILE:game_fallout4>"
	GAME_PLUGIN_FALLOUT4VR="$<TARGET_FILE:game_fallout4vr>"
	GAME_PLUGIN_STARFIELD="$<TARGET_FILE:game_starfield>")
target_link_libraries(game_bethesda_tests PRIVATE mo2::uibase)
add_dependencies(game_bethesda_tests game_skyrimse game_skyrim game_skyrimvr
	game_enderal game_enderalse game_oblivion game_fallout3 game_falloutnv
	game_fallout4 game_fallout4vr game_starfield)

# the save reader and the profile INIs of the static library game_gamebryo, in its own
# executable: the game plugins loaded by game_bethesda_tests have their own copy of
# game_gamebryo
add_executable(game_gamebryo_tests test_savegame_reader_linux.cpp
	test_profile_inis_linux.cpp)
mo2_configure_tests(game_gamebryo_tests NO_SOURCES WARNINGS OFF)
target_link_libraries(game_gamebryo_tests PRIVATE game_gamebryo)
