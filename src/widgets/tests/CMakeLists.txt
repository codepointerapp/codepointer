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

add_executable(LspTextEditTest test_lsp_text_edit.cpp)

target_include_directories(LspTextEditTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(LspTextEditTest
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

# The implementation under test. Kept as its own source file rather than part of
# the plugin so the tests can link it without the editor, and so swapping it for
# a Qt-free version is a one-line change here.
target_sources(LspTextEditTest PRIVATE ${CMAKE_SOURCE_DIR}/src/plugins/LSP/lsp_text_edit_qt.cpp)

add_test(NAME LspTextEditTest COMMAND LspTextEditTest)

# Drives a real language server, so it is opt-in: it needs clangd on PATH and
# takes seconds rather than milliseconds. Run with -DCODEPOINTER_BUILD_LSP_TESTS=ON.
if(CODEPOINTER_BUILD_LSP_TESTS)
    add_executable(LspRenameClangdTest test_lsp_rename_clangd.cpp)

    target_include_directories(LspRenameClangdTest PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/plugins/LSP
        ${CMAKE_SOURCE_DIR}/lib/lsp-framework
    )

    target_link_libraries(LspRenameClangdTest
        Qt6::Core
        Qt6::Gui
        Qt6::Test
        lsp
    )

    if(WIN32)
        # SHGetKnownFolderPath lives in shell32 and allocates through ole32; the
        # FOLDERID_* constants are defined in uuid. Only needed to locate the
        # well-known folders, but the test fails without them on Windows.
        target_link_libraries(LspRenameClangdTest
            PRIVATE
                shell32
                ole32
                uuid
        )
    endif()

    target_sources(LspRenameClangdTest PRIVATE
        ${CMAKE_SOURCE_DIR}/src/plugins/LSP/LspClientImpl.cpp
        ${CMAKE_SOURCE_DIR}/src/plugins/LSP/lsp_text_edit_qt.cpp
    )

    add_test(NAME LspRenameClangdTest COMMAND LspRenameClangdTest)
    # A server start is seconds, not milliseconds; the default 1500s is generous
    # but the suite would rather fail loudly than time out.
    set_tests_properties(LspRenameClangdTest PROPERTIES TIMEOUT 300)
endif()

add_executable(LspSyncedContentTest test_lsp_synced_content.cpp)

target_include_directories(LspSyncedContentTest PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(LspSyncedContentTest
    Qt6::Core
    Qt6::Test
)

add_test(NAME LspSyncedContentTest COMMAND LspSyncedContentTest)
