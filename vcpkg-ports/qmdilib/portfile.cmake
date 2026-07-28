# This is one of codepointer's own dependencies, developed alongside it.
# Rather than fetching a pinned commit from GitHub, this port builds
# directly from the local lib/qmdilib checkout (the same one get-code.sh
# populates) so day-to-day development doesn't require pushing every change
# upstream first. Qt itself is expected to already be available on
# CMAKE_PREFIX_PATH (system / aqtinstall Qt); it is not vcpkg-managed here.

set(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../lib/qmdilib")

if(NOT EXISTS "${SOURCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "lib/qmdilib is missing or empty. Run ./get-code.sh from the "
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
        -DQMDILIB_BUILD_EXAMPLES=OFF
        -DQMDILIB_TESTS=OFF
        -DQMDILIB_USE_CMAKEFORMAT=OFF
        -DQMDILIB_USE_MOLD=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(PACKAGE_NAME qmdilib CONFIG_PATH lib/cmake/qmdilib)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
