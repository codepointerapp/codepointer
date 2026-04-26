cmake_minimum_required(VERSION 3.20)
project(TreeSitterEngineTest)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core Test REQUIRED)

set(TS_ROOT ${CMAKE_SOURCE_DIR}/src/plugins/TreeSitter)

set_source_files_properties(
    ${TS_ROOT}/tree-sitter/lib/src/lib.c
    ${TS_ROOT}/tree-sitter-c/src/parser.c
    ${TS_ROOT}/tree-sitter-cpp/src/parser.c
    ${TS_ROOT}/tree-sitter-cpp/src/scanner.c
    PROPERTIES COMPILE_FLAGS "-w"
)

add_executable(TreeSitterEngineTest
    test_treesitter_engine.cpp
    ${TS_ROOT}/TreeSitterEngine.cpp
    ${TS_ROOT}/tree-sitter/lib/src/lib.c
    ${TS_ROOT}/tree-sitter-c/src/parser.c
    ${TS_ROOT}/tree-sitter-cpp/src/parser.c
    ${TS_ROOT}/tree-sitter-cpp/src/scanner.c
)

target_include_directories(TreeSitterEngineTest PRIVATE
    ${TS_ROOT}
    ${TS_ROOT}/tree-sitter/lib/include
)

target_link_libraries(TreeSitterEngineTest
    Qt6::Core
    Qt6::Test
)

add_test(NAME TreeSitterEngineTest COMMAND TreeSitterEngineTest)
