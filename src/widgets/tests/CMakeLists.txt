cmake_minimum_required(VERSION 3.20)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 COMPONENTS Core Gui Test REQUIRED)

add_executable(LspRenameTest test_lsp_rename.cpp)

target_include_directories(LspRenameTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(LspRenameTest
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME LspRenameTest COMMAND LspRenameTest)

add_executable(LspSyncedContentTest test_lsp_synced_content.cpp)

target_include_directories(LspSyncedContentTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(LspSyncedContentTest
    Qt6::Core
    Qt6::Test
)

add_test(NAME LspSyncedContentTest COMMAND LspSyncedContentTest)
