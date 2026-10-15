# Plugin load check for the Linux build: loads installed MO2 plugins (.so) the way the
# application's PluginContainer does (QPluginLoader) and checks the interfaces they
# implement. Built and run by build.sh --tests after each plugin target.
cmake_minimum_required(VERSION 3.22)

project(mo2_plugin_load LANGUAGES CXX)

find_package(mo2-cmake CONFIG REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)

add_executable(plugin_load main.cpp)
mo2_configure_target(plugin_load NO_SOURCES AUTOMOC OFF TRANSLATIONS OFF WARNINGS ON)
target_link_libraries(plugin_load PRIVATE mo2::uibase)
