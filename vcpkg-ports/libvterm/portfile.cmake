vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO neovim/libvterm
    REF 934bc2fbf21800ac3458a499df8820ca5fb45fd3
    SHA512 f1cc6dfba8ddd230792428384215c72361b1024f7029eaf38592277b363458e6f6d392585fd1810e9385fa7eb8962f8b739b18d9951fde3899ee95423e541b7b
    HEAD_REF master
)

# Upstream ships no CMake build (it's normally built via a Makefile). Drop in
# our own minimal CMakeLists.txt that mirrors what KodoTerm used to inline
# via FetchContent, so this becomes a normal, installable, find_package-able
# vcpkg package instead of a live git fetch during KodoTerm's own configure.
file(COPY
    "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_LIST_DIR}/vtermConfig.cmake.in"
    DESTINATION "${SOURCE_PATH}"
)

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
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(PACKAGE_NAME vterm CONFIG_PATH lib/cmake/vterm)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
