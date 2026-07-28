# QSimpleUpdater has no install rules of its own. This shim add_subdirectory()s
# the real source and installs the QSimpleUpdater target.

cmake_minimum_required(VERSION 3.20)
project(qsimpleupdater-vcpkg-shim LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(QSIMPLEUPDATER_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib/QSimpleUpdater")
if(NOT EXISTS "${QSIMPLEUPDATER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "lib/QSimpleUpdater is missing or empty. Run ./get-code.sh from the "
        "codepointer repo root before building with this overlay port.")
endif()

set(QSIMPLE_UPDATER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${QSIMPLEUPDATER_SOURCE_DIR}" qsimpleupdater-build)

install(TARGETS QSimpleUpdater
    EXPORT QSimpleUpdaterTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES "${QSIMPLEUPDATER_SOURCE_DIR}/include/QSimpleUpdater.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(EXPORT QSimpleUpdaterTargets
    FILE QSimpleUpdaterTargets.cmake
    NAMESPACE QSimpleUpdater::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QSimpleUpdater"
)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/QSimpleUpdaterConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/QSimpleUpdaterConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QSimpleUpdater"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/QSimpleUpdaterConfigVersion.cmake"
    VERSION 1.0.0
    COMPATIBILITY SameMajorVersion
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/QSimpleUpdaterConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/QSimpleUpdaterConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QSimpleUpdater"
)
