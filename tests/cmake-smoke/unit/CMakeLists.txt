add_executable(smoke_tests tests.cpp)
mo2_configure_tests(smoke_tests NO_SOURCES WARNINGS ON)
target_link_libraries(smoke_tests PRIVATE smoke)
