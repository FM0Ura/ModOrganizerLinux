cmake_minimum_required(VERSION 3.28)
# Consumer check of the installed libbsarchpp package (build-libbsarchpp.sh): the
# package must be found on its own, link statically into a shared object (PIC) and
# create and reopen an archive with the 7-argument constructor (spec of Phase 4e, §1.3).
project(libbsarchpp_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
find_package(libbsarchpp CONFIG REQUIRED)
add_library(consumer_shared SHARED main.cpp)
target_link_libraries(consumer_shared PRIVATE libbsarchpp)
add_executable(consumer main.cpp)
target_compile_definitions(consumer PRIVATE CONSUMER_MAIN)
target_link_libraries(consumer PRIVATE libbsarchpp)
