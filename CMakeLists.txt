cmake_minimum_required(VERSION 3.20)
project(Ferrous CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(inc)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(Ferrous ${SOURCES}
    inc/ast.hpp)
