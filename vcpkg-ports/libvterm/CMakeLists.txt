cmake_minimum_required(VERSION 3.20)
project(vterm C)

set(VTERM_VERSION "0.3.0")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

file(GLOB VTERM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(vterm STATIC ${VTERM_SOURCES})
target_include_directories(vterm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

install(TARGETS vterm
    EXPORT vtermTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES include/vterm.h include/vterm_keycodes.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(EXPORT vtermTargets
    FILE vtermTargets.cmake
    NAMESPACE vterm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vterm
)

configure_package_config_file(
    vtermConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/vtermConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vterm
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/vtermConfigVersion.cmake
    VERSION ${VTERM_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/vtermConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/vtermConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vterm
)
