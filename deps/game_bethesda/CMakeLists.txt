cmake_minimum_required(VERSION 3.16)

# TODO: clean include directives
set(MO2_CMAKE_DEPRECATED_UIBASE_INCLUDE ON)

project(game_bethesda)

find_package(mo2-cmake CONFIG REQUIRED)
find_package(Qt6 COMPONENTS Widgets)

add_subdirectory(src/gamebryo)
add_subdirectory(src/creation)

mo2_add_subdirectories(
    FOLDER games
    GLOB src/games/*
)

# Linux only: the tests check the Linux game detection
if (BUILD_TESTING AND NOT WIN32)
	enable_testing()
	add_subdirectory(tests)
endif()
