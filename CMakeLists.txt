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

set(QMDILIB_BUILD_EXAMPLES OFF)
set(QMDILIB_TESTS OFF)
set(QMDILIB_USE_CMAKEFORMAT OFF)
set(QUTEPART_TESTS OFF)
set(QUTEPART_USE_CMAKEFORMAT OFF)
set(QHEXVIEW_ENABLE_DIALOGS ON)
set(COMMAND_PALLETE_ENABLE_TESTS OFF)

option(CODEPOINTER_WORK_OFFLINE OFF)

if (CODEPOINTER_WORK_OFFLINE)
    # For this to work, you need to make these libraries available yourself.
    # On Linux/macOS, you can run the script `get-code.sh`. This might work on Windows using
    # git-bash, but is untested.
    add_subdirectory("lib/qmdilib")
    add_subdirectory("lib/qutepart-cpp")
    add_subdirectory("lib/command-palette-widget")
    add_subdirectory("lib/KodoTerm")
    add_subdirectory("lib/image-viewer")
    add_subdirectory("lib/QJsonModel")
    add_subdirectory("lib/QSimpleUpdater")
    add_subdirectory("lib/QHexView")
else()
    include(cmake/CPM.cmake)
    include(cmake/icons-breeze.cmake)

    CPMAddPackage("gh:diegoiast/qmdilib#23a174f6946f518e407552b3ce1368db13f97635")
    CPMAddPackage("gh:diegoiast/qutepart-cpp#2eea3e989a8f22be542002bc58f46c587eb9a412")
    CPMAddPackage("gh:diegoiast/command-palette-widget#69eb447b61c9d042394a1404c71a0d0b54ac28c0")
    CPMAddPackage("gh:diegoiast/KodoTerm#9d016815076a451ad639fa021d46602585e91247")
    CPMAddPackage("gh:palacaze/image-viewer#125ea784f93327b471cd9b6b156285e40e70280a")
    CPMAddPackage("gh:dridk/QJsonModel#f5fa5988c0ee52fff39a42973c964d5480951f86")
    CPMAddPackage("gh:alex-spataru/QSimpleUpdater#8e7017f7fbdc2b4b1a26ed1eef9ebcba6a50639c")
    CPMAddPackage("gh:Dax89/QHexView#beadd087413a46e7ed97afbab9dab1323ab1d033")
    download_breeze_icons(6.4.0)
endif()

find_package(Qt6 COMPONENTS Widgets PrintSupport Network Svg Xml)
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(codepointer_sources
    ${qmdilib_SOURCE_DIR}/demos/common/common.qrc
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/iplugin.cpp
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/pluginmodel.cpp
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/pluginmodel.h
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/plugin_list.ui
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/pluginmanager.cpp
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/pluginmanager.h
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/pluginwindow.ui
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/plugins/filesystem/filesystembrowser.cpp
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/plugins/filesystem/filesystembrowser.h
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/plugins/filesystem/filesystemwidget.cpp
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo/plugins/filesystem/filesystemwidget.h
    ${qutepart_SOURCE_DIR}/qutepart-syntax-files.qrc
    ${qutepart_SOURCE_DIR}/qutepart-theme-data.qrc

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

configure_file(codepointer.qrc.in ${CMAKE_BINARY_DIR}/codepointer.qrc)
configure_file(codepointer.desktop.in ${CMAKE_BINARY_DIR}/${CODEPOINTER_APP_NAME}.desktop)
add_executable(codepointer WIN32 ${codepointer_sources} codepointer.rc)

if (!MINGW)
    set_property(TARGET codepointerPROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
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
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo
    ${qutepart_SOURCE_DIR}/include
    src
)
target_link_libraries(codepointer PUBLIC Qt6::Core Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Svg Qt6::Xml
    qmdilib
    qutepart
    CommandPaletteWidget
    KodoTerm::KodoTerm
    Pal::ImageViewer
    QHexView
    QSimpleUpdater
    QJsonModelStatic
)
set_target_properties(codepointer PROPERTIES OUTPUT_NAME ${CODEPOINTER_BINARY})

if(MSVC)
   # if we don't do this - file will be under Debug/Relase subdir
   set_target_properties(codepointer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<0:>)
   target_compile_options(codepointer PRIVATE /W4)
else()
  set_target_properties(codepointer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  target_compile_options(codepointer PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS codepointer
        RUNTIME DESTINATION bin)

if (NOT CODEPOINTER_WORK_OFFLINE)
    install(DIRECTORY ${CMAKE_BINARY_DIR}/share/
            DESTINATION share/
            DIRECTORY_PERMISSIONS
                OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE)
endif()

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
