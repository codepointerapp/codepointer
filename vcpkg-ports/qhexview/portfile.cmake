# See CMakeLists.txt in this same directory for why a shim is used instead
# of building lib/QHexView's own CMakeLists.txt directly.

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
    SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}"
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(PACKAGE_NAME QHexView CONFIG_PATH lib/cmake/QHexView)

vcpkg_install_copyright(FILE_LIST "${CMAKE_CURRENT_LIST_DIR}/../../lib/QHexView/LICENSE")
