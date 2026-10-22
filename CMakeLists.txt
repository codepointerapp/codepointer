cmake_minimum_required(VERSION 3.20)
project (codepointer)

option(CODEPOINTER_BUNDLE_ICON_THEME "Should build use the icon theme, usually on" ON)
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

# Version the macOS bundle reports. Left empty by default on purpose: it is then
# read out of src/main.cpp, which is the one file bump-version.py already keeps in
# step with every other version in the project. A fifth copy to maintain would be a
# fifth thing to forget. CI passes the release tag, which outranks all of them.
set(CODEPOINTER_VERSION "" CACHE STRING "Version reported by the macOS bundle (default: from src/main.cpp)")

if(NOT CODEPOINTER_VERSION)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" CODEPOINTER_MAIN_CPP)
    string(REGEX MATCH "setApplicationVersion\\(\"([^\"]+)\"\\)" CODEPOINTER_VERSION_MATCH
           "${CODEPOINTER_MAIN_CPP}")
    set(CODEPOINTER_VERSION "${CMAKE_MATCH_1}")
    if(NOT CODEPOINTER_VERSION)
        message(WARNING "Could not find setApplicationVersion() in src/main.cpp")
    endif()
endif()

# Reduced to two values, because the bundle reports a version and a build number and
# they are not the same string.
#
# A release is split at its first "-" into a numeric core and a suffix:
#
#   0.9.2          core 0.9.2, build 0.9.2    - no suffix, nothing to add
#   0.9.2-3        core 0.9.2, build 0.9.2.3  - third build of 0.9.2
#   0.9.2-alpha5   core 0.9.2, build 0.9.2.0  - a prerelease, pinned to build 0
#
# That keeps the build numbers increasing across prereleases (0.9.2.0 < 0.9.2.3 <
# 0.9.3.0), which is what an updater compares, while the version stays a plain
# three-integer string as CFBundleShortVersionString requires.
string(REGEX REPLACE "^v" "" _raw_version "${CODEPOINTER_VERSION}")

string(FIND "${_raw_version}" "-" _dash_at)
if(_dash_at EQUAL -1)
    set(_core "${_raw_version}")
    set(_suffix "")
else()
    string(SUBSTRING "${_raw_version}" 0 ${_dash_at} _core)
    math(EXPR _suffix_at "${_dash_at} + 1")
    string(SUBSTRING "${_raw_version}" ${_suffix_at} -1 _suffix)
endif()

# The core has to end up numeric. Trailing junk is dropped rather than rejected, so
# a version written as 0.1.9beta still gives 0.1.9 - but with nothing numeric at all
# there is no sensible core and the whole thing falls back below.
string(REGEX REPLACE "[^0-9.].*$" "" _core "${_core}")

if(NOT _core MATCHES "^[0-9]+(\\.[0-9]+)*$")
    message(WARNING "'${CODEPOINTER_VERSION}' has no numeric version part to use; "
                    "the macOS bundle will report 0.0.0")
    set(_core "0.0.0")
    set(_suffix "")
endif()

if(_suffix STREQUAL "")
    set(_build "${_core}")
elseif(_suffix MATCHES "^[0-9]+$")
    set(_build "${_core}.${_suffix}")
else()
    # Not a build number, just a word - alpha, rc, beta. Build 0, and the version
    # it belongs to is carried by the core.
    set(_build "${_core}.0")
endif()

set(CODEPOINTER_APP_VERSION "${_core}")
set(CODEPOINTER_BUILD_NUMBER "${_build}")

# Printed on every platform, not just macOS, so a CI log says what went into the
# bundle without having to open the plist.
message(STATUS "App version: ${CODEPOINTER_APP_VERSION} (build ${CODEPOINTER_BUILD_NUMBER})")

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

set(QMDILIB_BUILD_EXAMPLES off)
set(QMDILIB_TESTS off)
set(DISABLE_QUTEPART_TESTS on)
set(QHEXVIEW_ENABLE_DIALOGS on)
set(LSP_USE_SANITIZERS off)
option(QTEDIT4_WORK_OFFLINE "Making this off will force using CPM, if set on, first run get-code.sh" ON)

option(CODEPOINTER_WORK_OFFLINE OFF)
option(CODEPOINTER_BUNDLE_ICON_THEME "Download and install the Breeze icon theme (needed when no system icon theme is present, e.g. bare Linux installs or Windows)" ON)
option(CODEPOINTER_DISABLE_UPDATES "Disable the in-app update checker (e.g. for Flatpak/store builds, where the store manages updates)" OFF)

# Disable testing for tree-sitter to avoid "ts-test" target collision between grammars
set(OLD_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING ${OLD_BUILD_TESTING} CACHE BOOL "" FORCE)

if (CODEPOINTER_WORK_OFFLINE)
    # For this to work, you need to make these libraries available yourself.
    # On Linux/macOS, you can run the script `get-code.sh`. Tested also on
    # git/bash form Git on Windows.
    add_subdirectory("lib/qmdilib")
    add_subdirectory("lib/qutepart-cpp")
    add_subdirectory("lib/command-palette-widget")
    add_subdirectory("lib/KodoTerm")
    add_subdirectory("lib/image-viewer")
    add_subdirectory("lib/QJsonModel")
    add_subdirectory("lib/QSimpleUpdater")
    add_subdirectory("lib/QHexView")
    add_subdirectory("lib/lsp-framework/")
else()
    include(cmake/CPM.cmake)
    include(cmake/icons-breeze.cmake)

    CPMAddPackage("gh:diegoiast/qutepart-cpp#020ad667612e755d4aefa2bc39fbf35ad1c282cd")
    CPMAddPackage("gh:diegoiast/qmdilib#17a534b77b4acd4a1d1ec03fdd7e46cb2fdb77cd")
    CPMAddPackage("gh:diegoiast/command-palette-widget#dc9e7ff8b632de9b7e49c25b5b95dc8693937300")
    CPMAddPackage("gh:diegoiast/KodoTerm#9d016815076a451ad639fa021d46602585e91247")
    CPMAddPackage("gh:palacaze/image-viewer#125ea784f93327b471cd9b6b156285e40e70280a")
    CPMAddPackage("gh:dridk/QJsonModel#ee005a26089fe5ab32a73abb9bca89ff758af302")
    CPMAddPackage("gh:alex-spataru/QSimpleUpdater#c5c136c0f79ca6423b0914a2676ae3d0e3811c5b")
    CPMAddPackage("gh:Dax89/QHexView#8893881350ea1d0bb0deafb7ae4a700e8de6683f")
    CPMAddPackage("gh:leon-bckl/lsp-framework#db64fac3eea5b36888b801cc0c8b081a377f1914")
    if (CODEPOINTER_BUNDLE_ICON_THEME)
        download_breeze_icons(6.4.0)
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg Xml Concurrent)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# FIXME: the plugin-demo framework (iplugin, pluginmanager, filesystembrowser)
# should be a proper exported target inside qmdilib, not demo source files
# compiled directly into consumers. Move them out of demos/ into qmdilib's
# library so they can be consumed via target_link_libraries without needing
# ${qmdilib_SOURCE_DIR}.
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
    src/widgets/SearchableMenuButton.cpp
    src/widgets/SearchableMenuButton.hpp
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

    src/plugins/LSP/LspClientImpl.cpp
    src/plugins/LSP/LspClientImpl.hpp
    src/plugins/LSP/LspPlugin.cpp
    src/plugins/LSP/LspPlugin.hpp
    src/plugins/LSP/lsp_diagnostic.cpp
    src/plugins/LSP/lsp_diagnostic.hpp
    src/plugins/LSP/lsp_text_edit_qt.cpp
    src/plugins/LSP/lsp_text_edit.hpp
    src/plugins/LSP/LspDebugWidget.cpp
    src/plugins/LSP/LspDebugWidget.hpp
    src/plugins/LSP/LspReferencesWidget.cpp
    src/plugins/LSP/LspReferencesWidget.hpp

    src/main.cpp
    ${CMAKE_BINARY_DIR}/codepointer.qrc
    indenters.json
    codepointer.rc

)

file(COPY "${CMAKE_SOURCE_DIR}/${ICON_NAME}.svg" DESTINATION "${CMAKE_BINARY_DIR}")
file(COPY "${CMAKE_SOURCE_DIR}/${ICON_NAME}.ico" DESTINATION "${CMAKE_BINARY_DIR}")
file(COPY "${CMAKE_SOURCE_DIR}/indenters.json" DESTINATION "${CMAKE_BINARY_DIR}/share/${CODEPOINTER_APP_NAME}/")
file(COPY "${CMAKE_SOURCE_DIR}/src/plugins/LSP/lsp-servers.json" DESTINATION "${CMAKE_BINARY_DIR}/share/${CODEPOINTER_APP_NAME}/")

configure_file(codepointer.qrc.in ${CMAKE_BINARY_DIR}/codepointer.qrc)
configure_file(codepointer.desktop.in ${CMAKE_BINARY_DIR}/${CODEPOINTER_APP_NAME}.desktop)
configure_file(org.codepointer.codepointer.appdata.xml.in
    ${CMAKE_BINARY_DIR}/org.codepointer.${CODEPOINTER_APP_NAME}.appdata.xml @ONLY)
add_executable(codepointer WIN32 MACOSX_BUNDLE ${codepointer_sources})

# NOTE: this used to read `if (!MINGW)`. CMake has no `!` operator - it treated
# "!MINGW" as the name of an undefined variable, so the branch never ran and LTO
# was silently off in every build, including the official one.
# Only enabled for the optimised configurations: LTO on a Debug build costs link
# time and buys nothing.
include(CheckIPOSupported)
check_ipo_supported(RESULT CODEPOINTER_IPO_SUPPORTED OUTPUT CODEPOINTER_IPO_ERROR)
if (CODEPOINTER_IPO_SUPPORTED AND NOT MINGW)
    set_property(TARGET codepointer PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET codepointer PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
    set_property(TARGET codepointer PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE)
else()
    message(STATUS "codepointer: LTO disabled (${CODEPOINTER_IPO_ERROR})")
endif()

target_compile_definitions(codepointer PRIVATE -DCODEPOINTER_APP_NAME="${CODEPOINTER_APP_NAME}")
target_compile_definitions(codepointer PRIVATE -DCODEPOINTER_ICON=":${CODEPOINTER_ICON}.ico")
if (CODEPOINTER_DISABLE_UPDATES)
    target_compile_definitions(codepointer PRIVATE -DDISABLE_UPDATES)
endif()
target_compile_definitions(codepointer PRIVATE -DCODEPOINTER_SVG_ICON=":${CODEPOINTER_ICON}.svg")
target_compile_definitions(codepointer PRIVATE -DCODEPOINTER_MACHINE_NAME="${CODEPOINTER_MACHINE_NAME}")

if(BUILD_VERSION STREQUAL "DEV")
    target_compile_definitions(codepointer PRIVATE -DBUILD_DEV)
elseif(BUILD_VERSION STREQUAL "CE")
    target_compile_definitions(codepointer PRIVATE -DBUILD_CE)
else()
    target_compile_definitions(codepointer PRIVATE -DBUILD_OFFICIAL)
endif()

# FIXME: ${qmdilib_SOURCE_DIR}/demos/plugin-demo should not be needed here.
# Once the plugin-demo framework is a proper qmdilib target this line goes away.
target_include_directories(codepointer PUBLIC
    ${qmdilib_SOURCE_DIR}/demos/plugin-demo
    src/plugins/TreeSitter/tree-sitter/lib/include
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
    lsp
)
set_target_properties(codepointer PROPERTIES OUTPUT_NAME ${CODEPOINTER_BINARY})

if(APPLE)
    # The icon is generated rather than committed: .icns is a format only macOS
    # tooling produces, and generating it from the SVG means there is one master
    # asset. Built at build time rather than configure time so that editing the
    # SVG regenerates it without re-running cmake.
    #
    # The SVG is the source, not the .ico, because the .ico stops at 256px and an
    # icns wants 512 and 1024 - upsampled from 256 those are visibly soft in the
    # Dock. cmake/make-icns.sh renders each size natively when librsvg is
    # available and falls back to the .ico when it is not, so a machine without
    # either still gets an icon instead of a broken build.
    set(CODEPOINTER_ICNS "${CMAKE_BINARY_DIR}/${CODEPOINTER_ICON}.icns")
    set(CODEPOINTER_ICNS_KEY "")
    find_program(CODEPOINTER_RSVG rsvg-convert)
    find_program(CODEPOINTER_SIPS sips)
    set(_icns_svg "${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.svg")
    set(_icns_ico "${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.ico")
    set(_icns_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/make-icns.sh")

    if((CODEPOINTER_RSVG OR CODEPOINTER_SIPS) AND EXISTS "${_icns_svg}")
        add_custom_command(
            OUTPUT "${CODEPOINTER_ICNS}"
            COMMAND "${_icns_script}" "${_icns_svg}" "${_icns_ico}" "${CODEPOINTER_ICNS}"
            DEPENDS "${_icns_svg}" "${_icns_script}"
            COMMENT "Generating ${CODEPOINTER_ICON}.icns"
            VERBATIM)
        # The bundle copies the icon while it is built, and cmake has no way to know
        # that depends on this output, so the dependency is stated.
        add_custom_target(codepointer_icns ALL DEPENDS "${CODEPOINTER_ICNS}")
        add_dependencies(codepointer codepointer_icns)
        set(CODEPOINTER_ICNS_KEY
            "  <key>CFBundleIconFile</key>\n  <string>${CODEPOINTER_ICON}</string>")
        if(CODEPOINTER_RSVG)
            message(STATUS "macOS bundle icon: from the SVG at every size (librsvg)")
        else()
            message(STATUS "macOS bundle icon: no rsvg-convert, so from qlmanage or "
                           "the .ico (install librsvg for a native render at every size)")
        endif()
    else()
        message(STATUS "macOS bundle icon: no rasteriser and no sips, using the generic icon")
    endif()

    # The bundle identifier has to be in the plist itself. Setting the
    # MACOSX_BUNDLE_GUI_IDENTIFIER property below does not put it there, because
    # cmake only merges that property into a plist it generates itself - hand it a
    # MACOSX_BUNDLE_INFO_PLIST and your file is used as-is.
    #
    # It matters beyond cosmetic reasons: QTBUG-135882 - macOS declines to present
    # NSOpenPanel and NSSavePanel for an application with no identifier, so every
    # file dialog silently did nothing. That is why this was left to an undefined
    # @CODEPOINTER_BUNDLE_ID@ and configure_file substituted it with nothing.
    set(CODEPOINTER_BUNDLE_ID "org.codepointer.${CODEPOINTER_APP_NAME}")
    set(CODEPOINTER_BUILD_FLAVOUR "${BUILD_VERSION}")

    configure_file(codepointer-Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist @ONLY)

    # Check what configure_file actually produced. Two ways this has gone wrong, both
    # silent: a placeholder nobody defined, and an identifier that rendered empty.
    # Either leaves macOS with an application it will not present NSOpenPanel for,
    # so every file dialog does nothing and nothing says why (QTBUG-135882).
    file(READ "${CMAKE_BINARY_DIR}/Info.plist" CODEPOINTER_GENERATED_PLIST)

    # Whitespace is removed before matching because the plist is indented across
    # lines, and a pattern of spaces does not cover the newline between the key and
    # its value - which makes a perfectly good plist look broken.
    string(REGEX REPLACE "[ \t\r\n]" "" CODEPOINTER_FLAT_PLIST
           "${CODEPOINTER_GENERATED_PLIST}")

    if(CODEPOINTER_FLAT_PLIST MATCHES "@[A-Z_]+@")
        string(REGEX MATCHALL "@[A-Z_]+@" CODEPOINTER_LEFTOVER
               "${CODEPOINTER_GENERATED_PLIST}")
        list(REMOVE_DUPLICATES CODEPOINTER_LEFTOVER)
        message(FATAL_ERROR
            "Info.plist still contains unsubstituted placeholders: ${CODEPOINTER_LEFTOVER}. "
            "Each needs a set() in this file, or configure_file substituted it with nothing.")
    endif()
    if(NOT CODEPOINTER_FLAT_PLIST MATCHES
       "<key>CFBundleIdentifier</key><string>[^<]+</string>")
        message(FATAL_ERROR
            "Info.plist has an empty CFBundleIdentifier. macOS will not present native "
            "file dialogs for an application without one, so File > Open and Save As "
            "would silently do nothing (QTBUG-135882).")
    endif()

    message(STATUS "Info.plist: bundle id ${CODEPOINTER_BUNDLE_ID}, version "
                   "${CODEPOINTER_APP_VERSION} (build ${CODEPOINTER_BUILD_NUMBER})")
    set_target_properties(codepointer PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME ${CODEPOINTER_APP_NAME}
        MACOSX_BUNDLE_GUI_IDENTIFIER org.codepointer.${CODEPOINTER_APP_NAME}
        MACOSX_BUNDLE_BUNDLE_VERSION ${CODEPOINTER_BUILD_NUMBER}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${CODEPOINTER_APP_VERSION}
        # The frameworks macdeployqt copies live in Contents/Frameworks and the
        # executable in Contents/MacOS, so the search path is one level up from the
        # loader. @loader_path rather than @executable_path because it resolves
        # relative to whichever binary is loading - the app itself or a plugin.
        INSTALL_RPATH "@loader_path/../Frameworks")

    # Qt 6 sets up rpaths and deployment from inside qt_add_executable(). This target
    # is declared with plain add_executable(), which does not do that, so without this
    # call nothing records an LC_RPATH and the app dies at launch with "Library not
    # loaded: @rpath/QtWidgets.framework" even though macdeployqt put the frameworks
    # right there. Only on Apple: the Windows build drives windeployqt itself from
    # build.bat and the CI workflow, and this would change what that produces.
    qt_finalize_executable(codepointer)
endif()

if(MSVC)
   # if we don't do this - file will be under Debug/Relase subdir
   set_target_properties(codepointer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}$<0:>)
   target_compile_options(codepointer PRIVATE /W4)

   set_source_files_properties(
       src/plugins/TreeSitter/tree-sitter/lib/src/lib.c
       PROPERTIES COMPILE_FLAGS "/W0"
   )
elseif(NOT APPLE)
  set_target_properties(codepointer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  target_compile_options(codepointer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
    # BUNDLE DESTINATION, not RUNTIME: for a MACOSX_BUNDLE target the executable
    # belongs inside Contents/MacOS and CMake decides that, so there is no RUNTIME
    # destination to give.
    install(TARGETS codepointer
            BUNDLE DESTINATION .)

    # After the bundle, and that ordering is the whole point: cmake runs install
    # rules in declaration order, so installing the icon first wrote it into a
    # Contents/Resources the bundle had not created yet, and installing the bundle
    # then filled that directory from the build tree and threw the icon away.
    #
    # CFBundleIconFile in the plist only names the icon, it does not put it in the
    # bundle. MACOSX_BUNDLE_ICON_FILE would have embedded it at build time, but that
    # property resolves against the source directory while the icns is generated
    # into the build tree, hence an explicit install into the finished bundle.
    install(FILES "${CODEPOINTER_ICNS}"
            DESTINATION ${CODEPOINTER_BINARY}.app/Contents/Resources)
else()
    install(TARGETS codepointer
            RUNTIME DESTINATION bin)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(codepointer PRIVATE -Wno-unknown-attributes -Wno-c++98-compat-local-type-template-args)
    target_compile_options(lsp PRIVATE -Wno-unknown-attributes -Wno-c++98-compat-local-type-template-args)
endif()


# The app looks for its bundled icons at applicationDirPath() + "/../share/icons"
# (src/main.cpp), which in a bundle resolves inside Contents/. Installed to the
# prefix root instead, they would sit outside the .app and never be found - and
# macOS has no freedesktop theme to fall back on, so the app would lose its icons
# entirely rather than merely look untidy.
if (NOT CODEPOINTER_WORK_OFFLINE AND CODEPOINTER_BUNDLE_ICON_THEME)
    if(APPLE)
        install(DIRECTORY ${CMAKE_BINARY_DIR}/share/
                DESTINATION ${CODEPOINTER_BINARY}.app/Contents/share
                DIRECTORY_PERMISSIONS
                    OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE)
    else()
        install(DIRECTORY ${CMAKE_BINARY_DIR}/share/
                DESTINATION share/
                DIRECTORY_PERMISSIONS
                    OWNER_READ OWNER_WRITE OWNER_EXECUTE
                    GROUP_READ GROUP_EXECUTE
                    WORLD_READ WORLD_EXECUTE)
    endif()
endif()

# Runtime data (indenters.json, lsp-servers.json). Always installed, independent
# of the icon theme: the app looks these up at <app dir>/share/<app name>/.
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/codepointer/
        DESTINATION share/codepointer)

if (WIN32)
    install(FILES ${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.svg
            DESTINATION .)
    install(FILES ${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.ico
            DESTINATION .)
else()
    install(FILES ${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.svg
            DESTINATION share/icons/hicolor/scalable/apps)
    install(FILES ${CMAKE_BINARY_DIR}/${CODEPOINTER_APP_NAME}.desktop
            DESTINATION share/applications
            RENAME org.codepointer.${CODEPOINTER_APP_NAME}.desktop)

    install(FILES ${CMAKE_SOURCE_DIR}/${CODEPOINTER_ICON}.svg
            DESTINATION share/icons/hicolor/scalable/apps
            RENAME org.codepointer.${CODEPOINTER_APP_NAME}.svg)
    install(FILES ${CMAKE_BINARY_DIR}/org.codepointer.${CODEPOINTER_APP_NAME}.appdata.xml
            DESTINATION share/metainfo)
endif()

option(CODEPOINTER_BUILD_TESTS "Build unit tests" OFF)
if(CODEPOINTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/plugins/TreeSitter/tests)
    add_subdirectory(src/widgets/tests)
endif()
