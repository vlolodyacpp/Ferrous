cmake_minimum_required(VERSION 3.30)

project(Ferrous
 VERSION 0.1
 LANGUAGES C CXX
)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_DIR}")
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")

add_executable(Ferrous
 src/main.cpp
)

target_compile_features(Ferrous PRIVATE
 cxx_std_23
)

target_compile_options(Ferrous PRIVATE
 -Wall -Wextra -Wpedantic -g
 -fno-rtti
 ${LLVM_DEFINITIONS_LIST}
)

target_include_directories(Ferrous PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_directories(Ferrous PRIVATE ${LLVM_LIBRARY_DIRS})
target_link_libraries(Ferrous PRIVATE LLVM)

# Абсолютный путь к исходнику рантайма вшивается в бинарь, чтобы компилятор
# работал из любого каталога, а не только из корня репозитория (C8).
target_compile_definitions(Ferrous PRIVATE
    FERROUS_RT_SRC="${CMAKE_SOURCE_DIR}/rt/ferrous_rt.cpp")

# Runtime-библиотека (статическая, линкуется с каждым .fer-бинарём)
add_library(ferrous_rt STATIC rt/ferrous_rt.cpp)
target_compile_features(ferrous_rt PRIVATE cxx_std_23)

add_custom_target(run
    COMMAND $<TARGET_FILE:Ferrous>
            ${CMAKE_SOURCE_DIR}/examples/test_arithmetic.fer
            --dump-ast
    DEPENDS Ferrous
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_custom_target(debug
    COMMAND ${CMAKE_COMMAND} -E echo
            "Reconfigure with: cmake -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug")

target_sources(Ferrous
    PRIVATE
        src/lexer.cpp
        src/parser.cpp
        src/printer.cpp
        src/semantic.cpp
        src/codegen.cpp
  PRIVATE FILE_SET CXX_MODULES FILES
        mod/ast.cppm
        mod/lexer.cppm
        mod/parser.cppm
        mod/token.cppm
        mod/printer.cppm
        mod/semantic.cppm
        mod/codegen.cppm
)
