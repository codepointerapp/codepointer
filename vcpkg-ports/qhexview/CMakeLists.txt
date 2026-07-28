# QHexView has no install rules of its own. This shim add_subdirectory()s
# the real source and installs the QHexView target, matching the
# QHEXVIEW_ENABLE_DIALOGS=ON setting codepointer's own CMakeLists.txt uses.

cmake_minimum_required(VERSION 3.20)
project(qhexview-vcpkg-shim LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(QHEXVIEW_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib/QHexView")
if(NOT EXISTS "${QHEXVIEW_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "lib/QHexView is missing or empty. Run ./get-code.sh from the "
        "codepointer repo root before building with this overlay port.")
endif()

set(QHEXVIEW_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(QHEXVIEW_ENABLE_DIALOGS ON CACHE BOOL "" FORCE)
add_subdirectory("${QHEXVIEW_SOURCE_DIR}" qhexview-build)

install(TARGETS QHexView
    EXPORT QHexViewTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(DIRECTORY "${QHEXVIEW_SOURCE_DIR}/include/QHexView"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(EXPORT QHexViewTargets
    FILE QHexViewTargets.cmake
    NAMESPACE QHexView::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QHexView"
)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/QHexViewConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/QHexViewConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QHexView"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/QHexViewConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/QHexViewConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/QHexViewConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QHexView"
)
