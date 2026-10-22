cmake_minimum_required(VERSION 3.16)

add_subdirectory(python)
add_subdirectory(runner)

if (NOT WIN32)
	# Linux: the test targets are not in the default build (EXCLUDE_FROM_ALL) and ctest
	# does not build them, so this target does
	add_custom_target(plugin_python-tests ALL)
	add_dependencies(plugin_python-tests runner-tests runner-isolation-tests
		runner-failure-tests runner-sibling-tests python-tests)
endif()
