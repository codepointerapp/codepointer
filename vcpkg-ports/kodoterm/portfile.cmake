# See vcpkg-ports/qmdilib/portfile.cmake for why this builds from the local
# checkout instead of fetching a pinned commit from GitHub.
#
# The libvterm dependency (declared in vcpkg.json) is installed before this
# port builds, so KodoTerm's own find_package(vterm CONFIG QUIET) check
# picks it up and skips its FetchContent fallback -- no network access
# needed during this port's build.

set(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../lib/KodoTerm")

if(NOT EXISTS "${SOURCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "lib/KodoTerm is missing or empty. Run ./get-code.sh from the "
        "codepointer repo root before building with this overlay port.")
endif()

# Put each function/class member in its own linker section so the final
# codepointer executable (built with -Wl,--gc-sections) can discard whatever
# of this library it never actually calls.
if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    string(APPEND VCPKG_C_FLAGS " /Gw")
    string(APPEND VCPKG_CXX_FLAGS " /Gw")
    string(APPEND VCPKG_LINKER_FLAGS " /OPT:REF /OPT:ICF")
else()
    string(APPEND VCPKG_C_FLAGS " -ffunction-sections -fdata-sections")
    string(APPEND VCPKG_CXX_FLAGS " -ffunction-sections -fdata-sections")
    string(APPEND VCPKG_LINKER_FLAGS " -Wl,--gc-sections")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(PACKAGE_NAME KodoTerm CONFIG_PATH lib/cmake/KodoTerm)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
