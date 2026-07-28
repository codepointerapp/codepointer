# QJsonModel has no install rules of its own. This shim add_subdirectory()s
# the real source and installs the one target codepointer actually uses
# (QJsonModelStatic, which statically embeds the QJsonModel OBJECT library's
# compiled sources), exporting it under our own QJsonModel:: namespace.

cmake_minimum_required(VERSION 3.26)
project(qjsonmodel-vcpkg-shim VERSION 0.0.2 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(QJSONMODEL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib/QJsonModel")
if(NOT EXISTS "${QJSONMODEL_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "lib/QJsonModel is missing or empty. Run ./get-code.sh from the "
        "codepointer repo root before building with this overlay port.")
endif()

add_subdirectory("${QJSONMODEL_SOURCE_DIR}" qjsonmodel-build)

install(TARGETS QJsonModelStatic QJsonModel
    EXPORT QJsonModelTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    OBJECTS DESTINATION "${CMAKE_INSTALL_LIBDIR}/qjsonmodel-objects"
)
install(FILES "${QJSONMODEL_SOURCE_DIR}/include/QJsonModel.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(EXPORT QJsonModelTargets
    FILE QJsonModelTargets.cmake
    NAMESPACE QJsonModel::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QJsonModel"
)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/QJsonModelConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/QJsonModelConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QJsonModel"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/QJsonModelConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/QJsonModelConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/QJsonModelConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QJsonModel"
)
