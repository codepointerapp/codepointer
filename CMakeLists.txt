cmake_minimum_required(VERSION 3.20)
project (codepointer)

set(BUILD_VERSION "DEV" CACHE STRING "Build version (DEV, CE, OFFICIAL)")
set_property(CACHE BUILD_VERSION PROPERTY STRINGS DEV CE OFFICIAL)

if(BUILD_VERSION STREQUAL "DEV")
    set(APP_SUFFIX "-dev")
    set(ICON_NAME "codepointer-dev")
    set(BUILD_APP_NAME "codepointer-dev")
elseif(BUILD_VERSION STREQUAL "CE")
    set(APP_SUFFIX "-ce")
    set(ICON_NAME "codepointer-ce")
    set(BUILD_APP_NAME "codepointer-ce")
else()
    set(APP_SUFFIX "")
    set(ICON_NAME "codepointer")
    set(BUILD_APP_NAME "codepointer")
endif()

set(CODEPOINTER_APP_NAME "${BUILD_APP_NAME}")
set(CODEPOINTER_BINARY "codepointer${APP_SUFFIX}")
set(CODEPOINTER_ICON "${ICON_NAME}")
set(CODEPOINTER_MACHINE_NAME "${MACHINE_IDENTIFIER}")

set(CMAKE_VERBOSE_MAKEFILE OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(cmake/mold-linker.cmake)
include(cmake/icons-breeze.cmake)

# Disable testing for tree-sitter to avoid "ts-test" target collision between grammars
set(OLD_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING ${OLD_BUILD_TESTING} CACHE BOOL "" FORCE)

# Dependencies are resolved via vcpkg manifest mode (vcpkg.json +
# vcpkg-configuration.json's overlay-ports). Configure with
# -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake ;
# see vcpkg-ports/ for the port definitions (own repos build from the local
# lib/ checkout populated by get-code.sh, third-party repos fetch pinned
# commits from GitHub).
find_package(qmdilib CONFIG REQUIRED)
find_package(qutepart CONFIG REQUIRED)
find_package(CommandPaletteWidget CONFIG REQUIRED)
find_package(KodoTerm CONFIG REQUIRED)
find_package(PalImageViewer CONFIG REQUIRED)
find_package(QJsonModel CONFIG REQUIRED)
find_package(QSimpleUpdater CONFIG REQUIRED)
find_package(QHexView CONFIG REQUIRED)
download_breeze_icons(6.4.0)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Xml Concurrent)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(codepointer_sources
    src/widgets/bannermessage.ui
    src/widgets/bannerwidget.cpp
    src/widgets/bannerwidget.h
    src/widgets/HistoryLineEdit.cpp
    src/widgets/HistoryLineEdit.h
    src/widgets/BoldItemDelegate.cpp
    src/widgets/BoldItemDelegate.hpp
    src/widgets/gotolineform.ui
    src/widgets/bannermessage.ui
    src/widgets/replaceform.ui
    src/widgets/searchform.ui
    src/widgets/textoperationswidget.cpp
    src/widgets/textoperationswidget.h
    src/widgets/textoperationsadapter.h
    src/widgets/qttextoperationsadapter.cpp
    src/widgets/qttextoperationsadapter.h
    src/widgets/textpreview.cpp
    src/widgets/textpreview.h
    src/widgets/xmltreemodel.cpp
    src/widgets/xmltreemodel.h
    src/widgets/LoadingWidget.cpp
    src/widgets/LoadingWidget.hpp
    src/widgets/qmdiSplitTab.cpp
    src/widgets/qmdiSplitTab.h
    src/widgets/qmdieditor.cpp
    src/widgets/qmdieditor.h
    src/widgets/SplitTabWidget.cpp
    src/widgets/SplitTabWidget.h
    src/widgets/qmdiSplitTab.cpp
    src/widgets/qmdiSplitTab.h
    src/widgets/FilesList.cpp
    src/widgets/FilesList.hpp
    src/widgets/AutoShrinkLabel.cpp
    src/widgets/AutoShrinkLabel.hpp

    src/plugins/CodeFormat/CodeFormat.cpp
    src/plugins/CodeFormat/CodeFormat.hpp
    src/plugins/texteditor/texteditor_plg.cpp
    src/plugins/texteditor/texteditor_plg.h
    src/plugins/texteditor/thememanager.cpp
    src/plugins/texteditor/thememanager.h
    src/plugins/help/help_plg.cpp
    src/plugins/help/help_plg.h
    src/plugins/imageviewer/imageviewer_plg.cpp
    src/plugins/imageviewer/imageviewer_plg.h
    src/plugins/hexviewer/hexviewer_plg.cpp
    src/plugins/hexviewer/hexviewer_plg.h
    src/plugins/hexviewer/hextextoperationsadapter.cpp
    src/plugins/hexviewer/hextextoperationsadapter.h
    src/plugins/ProjectManager/BuildRunOutput.ui
    src/plugins/ProjectManager/CompilerOutputDecoders.cpp
    src/plugins/ProjectManager/CompilerOutputDecoders.h
    src/plugins/ProjectManager/kitdefinitions.h
    src/plugins/ProjectManager/kitdefinitions.cpp
    src/plugins/ProjectManager/kitdefinitionmodel.h
    src/plugins/ProjectManager/kitdefinitionmodel.cpp
    src/plugins/ProjectManager/kitdetector.h
    src/plugins/ProjectManager/kitdetector.cpp
    src/plugins/ProjectManager/ProjectBuildConfig.cpp
    src/plugins/ProjectManager/ProjectBuildConfig.h
    src/plugins/ProjectManager/ProjectIssuesWidget.h
    src/plugins/ProjectManager/ProjectIssuesWidget.cpp
    src/plugins/ProjectManager/ProjectIssuesWidget.ui
    src/plugins/ProjectManager/ProjectManagerPlg.cpp
    src/plugins/ProjectManager/ProjectManagerPlg.h
    src/plugins/ProjectManager/ProjectManagerGUI.ui
    src/plugins/ProjectManager/ProjectSearch.cpp
    src/plugins/ProjectManager/ProjectSearch.h
    src/plugins/ProjectManager/ProjectSearchGUI.ui
    src/plugins/CTags/CTagsPlugin.cpp
    src/plugins/CTags/CTagsPlugin.hpp
    src/plugins/CTags/CTagsLoader.cpp
    src/plugins/CTags/CTagsLoader.hpp
    src/plugins/TreeSitter/tree-sitter/lib/src/lib.c
    src/plugins/TreeSitter/tree-sitter-c/src/parser.c
    src/plugins/TreeSitter/tree-sitter-cpp/src/parser.c
    src/plugins/TreeSitter/tree-sitter-cpp/src/scanner.c
    src/plugins/TreeSitter/TreeSitterPlugin.cpp
    src/plugins/TreeSitter/TreeSitterPlugin.hpp
    src/plugins/TreeSitter/TreeSitterEngine.cpp
    src/plugins/TreeSitter/TreeSitterEngine.hpp

    src/plugins/SplitTabsPlugin/SplitTabsPlugin.cpp
    src/plugins/SplitTabsPlugin/SplitTabsPlugin.hpp
    src/plugins/git/CommitDelegate.hpp
    src/plugins/git/CommitDelegate.cpp
    src/plugins/git/CommitModel.hpp
    src/plugins/git/CommitModel.cpp
    src/plugins/git/CreateGitBranch.hpp
    src/plugins/git/CreateGitBranch.cpp
    src/plugins/git/CreateGitBranch.ui
    src/plugins/git/GitCommands.ui
    src/plugins/git/GitCommit.ui
    src/plugins/git/GitPlugin.cpp
    src/plugins/git/GitPlugin.hpp
    src/plugins/git/CommitForm.cpp
    src/plugins/git/CommitForm.hpp
    src/plugins/git/CommitForm.ui
    src/plugins/Terminal/TerminalPlugin.cpp
    src/plugins/Terminal/TerminalPlugin.hpp
    src/AnsiToHTML.cpp
    src/AnsiToHTML.hpp
    src/main.cpp
    ${CMAKE_BINARY_DIR}/codepointer.qrc
)

file(COPY "${CMAKE_SOURCE_DIR}/${ICON_NAME}.svg" DESTINATION "${CMAKE_BINARY_DIR}")
file(COPY "${CMAKE_SOURCE_DIR}/${ICON_NAME}.ico" DESTINATION "${CMAKE_BINARY_DIR}")
file(COPY "${CMAKE_SOURCE_DIR}/indenters.json" DESTINATION "${CMAKE_BINARY_DIR}")

configure_file(codepointer.qrc.in ${CMAKE_BINARY_DIR}/codepointer.qrc)
configure_file(codepointer.desktop.in ${CMAKE_BINARY_DIR}/${CODEPOINTER_APP_NAME}.desktop)
add_executable(codepointer WIN32 ${codepointer_sources} codepointer.rc
    indenters.json)

if (!MINGW)
    set_property(TARGET codepointer PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_compile_definitions(codepointer PRIVATE -DCODEPOINTER_APP_NAME="${CODEPOINTER_APP_NAME}")
target_compile_definitions(codepointer PRIVATE -DCODEPOINTER_ICON=":${CODEPOINTER_ICON}.ico")
target_compile_definitions(codepointer PRIVATE -DCODEPOINTER_SVG_ICON=":${CODEPOINTER_ICON}.svg")
target_compile_definitions(codepointer PRIVATE -DCODEPOINTER_MACHINE_NAME="${CODEPOINTER_MACHINE_NAME}")

if(BUILD_VERSION STREQUAL "DEV")
    target_compile_definitions(codepointer PRIVATE -DBUILD_DEV)
elseif(BUILD_VERSION STREQUAL "CE")
    target_compile_definitions(codepointer PRIVATE -DBUILD_CE)
else()
    target_compile_definitions(codepointer PRIVATE -DBUILD_OFFICIAL)
endif()

target_include_directories(codepointer PUBLIC
    src/plugins/TreeSitter/tree-sitter/lib/include
    src
)
target_link_libraries(codepointer PUBLIC Qt6::Core Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Svg Qt6::Xml
    qmdi::qmdilib
    qmdi::qmdi-pluginframework
    qutepart::qutepart
    CommandPaletteWidget::CommandPaletteWidget
    KodoTerm::KodoTerm
    Pal::ImageViewer
    QHexView::QHexView
    QSimpleUpdater::QSimpleUpdater
    QJsonModel::QJsonModelStatic
)
set_target_properties(codepointer PROPERTIES OUTPUT_NAME ${CODEPOINTER_BINARY})

if(MSVC)
   # if we don't do this - file will be under Debug/Relase subdir
   set_target_properties(codepointer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<0:>)
   target_compile_options(codepointer PRIVATE /W4 /Gw)
   target_link_options(codepointer PRIVATE /OPT:REF /OPT:ICF)

   set_source_files_properties(
       src/plugins/TreeSitter/tree-sitter/lib/src/lib.c
       PROPERTIES COMPILE_FLAGS "/W0"
   )
else()
  set_target_properties(codepointer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  target_compile_options(codepointer PRIVATE -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections)
  target_link_options(codepointer PRIVATE -Wl,--gc-sections)
endif()

install(TARGETS codepointer
        RUNTIME DESTINATION bin)

install(DIRECTORY ${CMAKE_BINARY_DIR}/share/
        DESTINATION share/
        DIRECTORY_PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)

if (WIN32)
    install(FILES ${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.svg
            DESTINATION /)
    install(FILES ${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.ico
            DESTINATION /)
else()
    install(FILES ${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.svg
            DESTINATION share/icons/hicolor/scalable/apps)
    install(FILES ${CMAKE_BINARY_DIR}/${CODEPOINTER_APP_NAME}.desktop
            DESTINATION share/applications
            RENAME org.codepointerapp.${CODEPOINTER_APP_NAME}.desktop)

    install(FILES ${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.svg
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME org.codepointerapp.${CODEPOINTER_APP_NAME}.svg)
    install(FILES org.codepointerapp.codepointer.appdata.xml
            DESTINATION share/metainfo)
endif()

option(CODEPOINTER_BUILD_TESTS "Build unit tests" OFF)
if(CODEPOINTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/plugins/TreeSitter/tests)
endif()
