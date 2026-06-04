cmake_minimum_required(VERSION 3.30)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
                                       
set(CMAKE_CXX_FLAGS "-stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++")


project(Ferrous 
	VERSION 0.1
	LANGUAGES CXX
)

add_executable(Ferrous
	src/main.cpp
		# placeholder for general source files
	# placeholder for module implementation units
)

set_target_properties(Ferrous PROPERTIES
	CXX_MODULE_STD ON
)

target_compile_features(Ferrous PRIVATE
	cxx_std_23
)


target_compile_options(Ferrous PRIVATE
	-Wall -Wextra -Wpedantic -g
	-stdlib=libc++ -Wno-dev
)

target_link_options(Ferrous PRIVATE -stdlib=libc++)

add_custom_target(run
    COMMAND $<TARGET_FILE:Ferrous>
            ${CMAKE_SOURCE_DIR}/examples/sum.fer
            --dump-ast
    DEPENDS Ferrous
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_custom_target(debug
    COMMAND ${CMAKE_COMMAND} -E echo
            "Reconfigure with: cmake -DCMAKE_BUILD_TYPE=Debug -B cmake-build-debug")

target_sources(Ferrous
    PRIVATE
        src/main.cpp
        src/lexer.cpp
        src/parser.cpp
		src/printer.cpp
		src/semantic.cpp
 	PRIVATE FILE_SET CXX_MODULES FILES
 		mod/ast.cppm
        mod/lexer.cppm
        mod/parser.cppm
        mod/token.cppm
		mod/printer.cppm
		mod/semantic.cppm
)