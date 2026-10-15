cmake_minimum_required(VERSION 3.16)

project(installer_fomod)

add_subdirectory(src)

# Linux only: the tests use the FOMOD sample of the ModOrganizerLinux tests
if (BUILD_TESTING AND NOT WIN32)
	enable_testing()
	add_subdirectory(tests)
endif()
