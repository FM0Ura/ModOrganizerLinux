cmake_minimum_required(VERSION 3.20)
project(check_7z CXX)

find_package(7zip 26.01 CONFIG REQUIRED)

add_executable(check_7z check_7z.cpp)
target_compile_features(check_7z PRIVATE cxx_std_17)
target_link_libraries(check_7z PRIVATE 7zip::7zip ${CMAKE_DL_LIBS})
