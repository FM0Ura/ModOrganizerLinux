cmake_minimum_required(VERSION 3.16)

set(MO2_CMAKE_DEPRECATED_UIBASE_INCLUDE ON)

project(bsa_packer)

add_subdirectory(src)

set(BUILD_TESTING ${BUILD_TESTING} CACHE BOOL "build tests for bsapacker plugin")
if(BUILD_TESTING)
	enable_testing()
	add_subdirectory(tests)
endif()

# Linux (spec of Phase 4e, section 3.4): the tests of the libbsarch replacement in linux/
if(BUILD_TESTING AND NOT WIN32)
	add_subdirectory(linux/tests)
endif()
