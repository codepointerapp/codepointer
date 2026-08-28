/**
 * \file kitdefinitions.cpp
 * \brief Definition of kit detector
 * \author Diego Iastrubni (diegoiast@gmail.com)
 *  License MIT
 */

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace KitDetector {

enum class Toolchain {
    Unknown,
    MSVC,
    MinGW,
    GCC,
    Clang,
    Rust,
};

struct ExtraPath {
    std::string name;
    std::string compiler_path;
    std::string comment;
    std::string command;
    Toolchain toolchain = Toolchain::Unknown;
};

#ifdef _WIN32
[[maybe_unused]] constexpr auto platformUnix = !true;
[[maybe_unused]] constexpr auto platformWindows = true;
#else
[[maybe_unused]] constexpr auto platformUnix = true;
[[maybe_unused]] constexpr auto platformWindows = !true;
#endif

auto findCompilers(bool unix_target) -> std::vector<ExtraPath>;
auto findQtVersions(bool unix_target, std::vector<ExtraPath> &detectedQt,
                    std::vector<ExtraPath> &detectedCompilers) -> void;
auto findCompilerTools(bool unix_target) -> std::vector<ExtraPath>;

auto deleteOldKitFiles(const std::filesystem::path &directoryPath) -> void;
auto generateKitFiles(const std::filesystem::path &directoryPath,
                      const std::vector<ExtraPath> &tools, const std::vector<ExtraPath> &compilers,
                      const std::vector<ExtraPath> &qtInstalls, bool unix_target) -> void;

} // namespace KitDetector
