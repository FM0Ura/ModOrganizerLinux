cmake_minimum_required(VERSION 3.16)

find_package(GTest REQUIRED)
find_package(mo2-cmake CONFIG REQUIRED)
find_package(mo2-uibase CONFIG REQUIRED)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# the FOMOD sample (fomod/ModuleConfig.xml and its two textures), see test_fomod_parse.cpp
set(FOMOD_SAMPLE_DIR "" CACHE PATH "directory of the FOMOD sample used by the tests")
if (NOT FOMOD_SAMPLE_DIR)
	message(FATAL_ERROR "FOMOD_SAMPLE_DIR must point to the FOMOD sample of the tests")
endif()

# the tests build the sources of the plugin (its classes are not exported) next to the
# files of this directory (mo2_configure_tests)
file(GLOB installer_fomod_sources CONFIGURE_DEPENDS
	${PROJECT_SOURCE_DIR}/src/*.cpp
	${PROJECT_SOURCE_DIR}/src/*.h
	${PROJECT_SOURCE_DIR}/src/*.ui
	${PROJECT_SOURCE_DIR}/src/*.qrc)

add_executable(installer_fomod_tests ${installer_fomod_sources})
mo2_configure_tests(installer_fomod_tests NO_MAIN WARNINGS OFF AUTOMOC ON)
target_include_directories(installer_fomod_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(installer_fomod_tests
	PRIVATE FOMOD_SAMPLE_DIR="${FOMOD_SAMPLE_DIR}")
target_link_libraries(installer_fomod_tests PRIVATE mo2::uibase)
