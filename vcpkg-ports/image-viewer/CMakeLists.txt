# image-viewer (palacaze/image-viewer) only enables install(TARGETS)/EXPORT
# when it detects it is the CMake top-level project (its PIV_STANDALONE
# check), and forces BUILD_SHARED_LIBS=ON in that same branch. Rather than
# building it as a shared library (a packaging change codepointer doesn't
# want), this shim add_subdirectory()s the real source -- which keeps
# PIV_STANDALONE off and the library static -- and then performs the same
# install/export steps image-viewer's own CMakeLists.txt would have done
# standalone, reusing its own PalImageViewerConfig.cmake.in.

cmake_minimum_required(VERSION 3.20)
project(image-viewer-vcpkg-shim VERSION 0.2 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(IMAGE_VIEWER_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../lib/image-viewer")
if(NOT EXISTS "${IMAGE_VIEWER_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "lib/image-viewer is missing or empty. Run ./get-code.sh from the "
        "codepointer repo root before building with this overlay port.")
endif()

add_subdirectory("${IMAGE_VIEWER_SOURCE_DIR}" image-viewer-build)

set(PIV_QT_DEPENDENCY "Qt6 COMPONENTS Core Gui Widgets")

install(TARGETS ImageViewer
    EXPORT PalImageViewerTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/image-viewer-build/include/pal/image-viewer-export.h"
        "${IMAGE_VIEWER_SOURCE_DIR}/include/pal/image-viewer.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pal"
)
install(EXPORT PalImageViewerTargets
    FILE PalImageViewerTargets.cmake
    NAMESPACE Pal::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PalImageViewer"
)

configure_package_config_file(
    "${IMAGE_VIEWER_SOURCE_DIR}/PalImageViewerConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PalImageViewerConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PalImageViewer"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PalImageViewerConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/PalImageViewerConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/PalImageViewerConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/PalImageViewer"
)
