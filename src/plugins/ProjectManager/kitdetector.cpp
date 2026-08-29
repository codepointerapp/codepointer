/**
 * \file kitdefinitions.cpp
 * \brief Implementation of kit detector
 * \author Diego Iastrubni (diegoiast@gmail.com)
 * SPDX-License-Identifier: MIT
 */

#include "kitdetector.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>

#if defined(__APPLE__) || defined(__MACH__) || defined(__unix__) || defined(__linux__)
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

constexpr auto HOME_DIR_ENV = "HOME";
constexpr auto BINARY_EXT = "";
constexpr auto ENV_SEPARATOR = ':';
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN

// MinGW already defines this
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <functional>
#include <qDebug>
#include <shlobj.h>
#include <windows.h>

constexpr auto HOME_DIR_ENV = "USERPROFILE";
constexpr auto BINARY_EXT = ".exe";
constexpr auto ENV_SEPARATOR = ';';
#endif

constexpr auto SCRIPT_EXTENSION_UNIX = ".sh";
constexpr auto SCRIPT_HEADER_UNIX = R"(#! /bin/sh

# This is a kit definition for CodePointer. All your tasks
# will run through this file.

# available environment variables:
# ${source_directory} - where the source being executed is saved
# ${build_directory}  - where code should be compile into
# ${run_directory}    - where this task should be run, as defined in
#                       the task's definition in the JSON file
# ${task}             - the actual code to be run

# The following meta variables are for CodePointer. Note the prefix:
# @@ name = @@NAME@@
# @@ author = auto generated - by CodePointer

# from this point on - echo is on. Every command will be displayed
# in the build output.
set -x
set -e

echo "Running from kit ${0}"
echo "Source is in        : ${source_directory}"
echo "Binaries will be    : ${build_directory}"
echo "Working directory is: ${run_directory}"
)";
constexpr auto SCRIPT_SUFFIX_UNIX = R"(# execute task
cd ${run_directory}
eval "${task}"
)";

constexpr auto SCRIPT_EXTENSION_WIN32 = ".bat";
constexpr auto SCRIPT_HEADER_WIN32 = R"(@echo off

rem This is a kit definition for CodePointer. All your tasks
rem will run through this file.

rem available environment variables:
rem %source_directory% - where the source being executed is saved
rem %build_directory%  - where code should be compile into
rem %run_directory%    - where this task should be run, as defined in
rem                      the task's definition in the JSON file
rem %task%             - the actual code to be run

rem The following meta variables are for codepointer Note the prefix:
rem @@ name = @@NAME@@
rem @@ author = auto generated - by CodePointer

rem from this point on - echo is on. Every command will be displayed
rem in the build output.
@echo on

@echo "Running from kit %0%"
@echo "Source is in        : %source_directory%"
@echo "Binaries will be in : %build_directory%"
@echo "Working directory is: %run_directory%"

)";
constexpr auto SCRIPT_SUFFIX_WIN32 = R"(
@rem execute task
cd %run_directory%
%task%
)";

namespace KitDetector {
/*
static auto is_command_in_path(const std::string &cmd,
                               const char delimiter = PLATFORM_PATH_DELIMITER) -> std::string {
    auto path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return {};
    }
    auto ss = std::stringstream(path_env);
    auto dir = std::string();
    while (std::getline(ss, dir, delimiter)) {
        auto full_path = dir + std::filesystem::path::preferred_separator + cmd;
        if (access(full_path.c_str(), X_OK) == 0) {
            return full_path;
        }
    }

    return {};
}
*/

static auto replaceAll(std::string &str, const std::string &from, const std::string &to)
    -> std::string & {
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
}

static auto safeGetEnv(const char *name) -> std::string {
#if defined(_WIN32)
    // TODO: we deal with ansi only - is this OK?
    // Limit according to http://msdn.microsoft.com/en-us/library/ms683188.aspx
    DWORD bufferSize = 65535;
    std::string buff;
    buff.resize(bufferSize);
    bufferSize = GetEnvironmentVariableA(name, &buff[0], bufferSize);
    if (bufferSize > 0) {
        buff.resize(bufferSize);
    } else {
        buff.clear();
    }
    return buff;
#else
    auto v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

auto getHomeDir() -> std::filesystem::path {
#ifdef _WIN32
    wchar_t szPath[MAX_PATH] = {0};
    DWORD result = GetEnvironmentVariableW(L"USERPROFILE", szPath, MAX_PATH);
    if (result > 0 && result < MAX_PATH) {
        return std::filesystem::path(szPath);
    }
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, szPath))) {
        return std::filesystem::path(szPath);
    }
#else
    const char *homeDirEnv = std::getenv("HOME");
    if (homeDirEnv) {
        return std::filesystem::path(homeDirEnv);
    }
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return std::filesystem::path(pw->pw_dir);
    }
#endif
    return {};
}

#ifdef WIN32_
#define CARGO "cargo.exe"
#else
#define CARGO "cargo"
#endif

using PathCallback = std::function<void(const std::filesystem::path &full_filename)>;

static auto findCommandInPath(const std::string &cmd, PathCallback callback) -> void {
    auto path_env = safeGetEnv("PATH");
    if (path_env.empty()) {
        return;
    }
    auto ss = std::stringstream(path_env);
    auto dir = std::string();
    while (std::getline(ss, dir, ENV_SEPARATOR)) {
        auto full_path = std::filesystem::path(dir) / std::filesystem::path(cmd);
#if defined(_WIN32)
        if (_waccess(full_path.c_str(), 04) != 0) {
            continue;
        }
#else
        if (access(full_path.c_str(), X_OK) != 0) {
            continue;
        }
#endif
        callback(full_path);
    }
    return;
}

auto static findRustSetup(std::vector<KitDetector::ExtraPath> &detected, bool unix_target) -> void {
    auto cargoHome = getHomeDir() / ".cargo";
    auto cargoHomeEnv = safeGetEnv("CARGO_HOME");
    if (!cargoHomeEnv.empty()) {
        cargoHome = std::filesystem::path(cargoHomeEnv);
    } else {
        findCommandInPath(CARGO, [&cargoHome](const auto &full_path) {
            // just handle the first one, unsure if this is the best solution
            if (cargoHome.empty()) {
                // cargoHome now points to "/usr/bin/cargo", and I need it to point to "/usr/"
                cargoHome = full_path.parent_path().parent_path().string();
            }
        });
    }
    auto cargoPath = cargoHome / "bin" / (unix_target ? "cargo" : "cargo.exe");
    if (std::filesystem::exists(cargoPath)) {
        auto extraPath = KitDetector::ExtraPath();
        extraPath.name = "Rust - Cargo";
        extraPath.compiler_path = cargoHome.string();
        extraPath.toolchain = Toolchain::Rust;
        extraPath.comment = "# found rust installation at '" + cargoHome.string() + "'";
        if (unix_target) {
            extraPath.command = "export PATH=\"" + extraPath.compiler_path + "/bin:${PATH}\"";
        } else {
            extraPath.command = "set PATH=" + extraPath.compiler_path + "\\bin;%PATH%";
        }
        detected.push_back(extraPath);
    }
}

#if defined(_WIN32)
static auto wstringToString(const std::wstring &wstr) -> std::string {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), nullptr, 0,
                                          nullptr, nullptr);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &str[0], size_needed, nullptr,
                        nullptr);
    return str;
}

static auto checkVisualStudioVersion(PWSTR basePath, const std::wstring &version,
                                     KitDetector::ExtraPath &extraPath) -> bool {
    std::wstring basePathW(basePath);
    std::filesystem::path versionPath =
        std::filesystem::path(basePathW) / "Microsoft Visual Studio" / version;
    if (std::filesystem::exists(versionPath)) {
        extraPath.name = "MSVC " + wstringToString(version);
        extraPath.compiler_path = wstringToString(versionPath);
        extraPath.comment = "@rem VS " + wstringToString(version);
        extraPath.command = "call %1\\VC\\Auxiliary\\Build\\vcvarsall.bat";
        extraPath.toolchain = KitDetector::Toolchain::MSVC;
        KitDetector::replaceAll(extraPath.command, "%1", versionPath.string());
        return true;
    }
    return false;
}

static auto findCppCompilersWindows(std::vector<KitDetector::ExtraPath> &detected) -> void {
    PWSTR programFiles = nullptr;
    PWSTR programFiles86 = nullptr;

    SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &programFiles);
    SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &programFiles86);

    // clang-format off
    auto versions = std::vector<std::wstring>{
        L"2022\\Community",
        L"2022\\Professional",
        L"2022\\Enterprise",
        L"2019\\Community",
        L"2019\\Professional",
        L"2019\\Enterprise"
    };
    // clang-format on

    for (const auto &version : versions) {
        auto extraPath = KitDetector::ExtraPath();
        if (programFiles[0] && checkVisualStudioVersion(programFiles, version, extraPath)) {
            detected.push_back(extraPath);
        }
        if (programFiles86[0] && checkVisualStudioVersion(programFiles86, version, extraPath)) {
            detected.push_back(extraPath);
        }
    }
    CoTaskMemFree(programFiles86);
    CoTaskMemFree(programFiles);
}

static auto findCompilerToolsWindows(std::vector<KitDetector::ExtraPath> &detected) -> void {
    auto cmakePath = std::filesystem::path();
    PWSTR programFiles = nullptr;
    PWSTR programFiles86 = nullptr;

    SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &programFiles);
    cmakePath = std::filesystem::path(programFiles) / "CMake" / "bin" / "cmake.exe";
    if (std::filesystem::exists(cmakePath)) {
        auto extraPath = KitDetector::ExtraPath();
        extraPath.name = "CMake";
        extraPath.compiler_path = (std::filesystem::path(programFiles) / "CMake" / "bin").string();
        extraPath.comment = "@rem Found CMake";
        extraPath.command = "set PATH=" + extraPath.compiler_path + ";%PATH%";
        detected.push_back(extraPath);
    }

    SHGetKnownFolderPath(FOLDERID_ProgramFilesX86, 0, NULL, &programFiles86);
    cmakePath = std::filesystem::path(programFiles86) / "CMake" / "bin" / "cmake.exe";
    if (std::filesystem::exists(cmakePath)) {
        auto extraPath = KitDetector::ExtraPath();
        extraPath.name = "CMake (x86)";
        extraPath.compiler_path =
            (std::filesystem::path(programFiles86) / "CMake" / "bin").string();
        extraPath.comment = "@rem Found CMake (x86)";
        extraPath.command = "set PATH=" + extraPath.compiler_path + ";%PATH%";
        detected.push_back(extraPath);
    }
}
#endif

[[maybe_unused]] static auto isCompilerAlreadyFound(const std::vector<ExtraPath> &detected,
                                                    const std::string &cc) -> bool {
    auto canonnical_cc = std::filesystem::canonical(cc);
    for (const auto &p : detected) {
        auto c = std::filesystem::canonical(p.compiler_path);
        if (c == canonnical_cc) {
            return true;
        }
    }
    return false;
}

auto isValidQtInstallation(const std::filesystem::path &path) -> bool {
    std::filesystem::path qmakePath = path / "bin" / (std::string("qmake") + BINARY_EXT);

    // check if this this is a false positite.
    auto ss = qmakePath.string();
    if (ss.find("qt6_design_studio_reduced_version") != std::string::npos) {
        return false;
    }
    return std::filesystem::exists(qmakePath);
}

auto static findCompilersImpl(std::vector<KitDetector::ExtraPath> &detected,
                              const std::string &cc_name, const std::string &cxx_name,
                              bool unix_target) -> void {
    std::set<std::string> real_compiler_paths;
    // clang is ambiguous on Windows (it may drive MSVC or MinGW), so it acts
    // as a wildcard there to keep pairing with either Qt flavour. GCC on the
    // other hand can never be MSVC.
    auto toolchain = Toolchain::Clang;
    if (cc_name == "gcc") {
#if defined(_WIN32)
        toolchain = Toolchain::MinGW;
#else
        toolchain = Toolchain::GCC;
#endif
    } else if (cc_name == "clang") {
#if defined(_WIN32)
        toolchain = Toolchain::Unknown;
#endif
    }

    // First detect postfixed compilers
    for (int version = 4; version < 20; version++) {
        auto cc = cc_name + "-" + std::to_string(version) + BINARY_EXT;

        findCommandInPath(cc, [&, version](const std::filesystem::path &full_path) {
            if (isCompilerAlreadyFound(detected, full_path.string())) {
                return;
            }

            std::error_code ec;
            auto resolved = std::filesystem::canonical(full_path, ec);
            if (!ec) {
                real_compiler_paths.insert(resolved.string());
            }

            KitDetector::ExtraPath extraPath;
            auto cxx = cxx_name + "-" + std::to_string(version);
            extraPath.name = cc;
            extraPath.compiler_path = full_path.string();
            extraPath.toolchain = toolchain;
            extraPath.comment = unix_target ? "# detected " + full_path.string()
                                            : "@rem detected " + full_path.string();
            extraPath.command += unix_target ? "export CC=" + cc + "\nexport CXX=" + cxx
                                             : "SET CC=" + cc + "\nSET CXX=" + cxx;
            detected.push_back(extraPath);
        });
    }

    // Now detect non-suffixed compiler only if not redundant
    auto cc = cc_name + BINARY_EXT;
    findCommandInPath(cc, [&](const std::filesystem::path &full_path) {
        std::error_code ec;
        auto resolved = std::filesystem::canonical(full_path, ec);
        if (!ec && real_compiler_paths.contains(resolved.string())) {
            return; // skip: already found postfixed version via symlink
        }

        if (isCompilerAlreadyFound(detected, full_path.string())) {
            return;
        }

        KitDetector::ExtraPath extraPath;
        auto cxx = cxx_name + BINARY_EXT;
        extraPath.name = cc;
        extraPath.compiler_path = full_path.string();
        extraPath.toolchain = toolchain;
        extraPath.comment = unix_target ? "# detected " + full_path.string()
                                        : "@rem detected " + full_path.string();
        extraPath.command += unix_target ? "export CC=" + cc + "\nexport CXX=" + cxx
                                         : "SET CC=" + cc + "\nSET CXX=" + cxx;
        detected.push_back(extraPath);
    });
}

auto static findCppCompilersInPath(std::vector<KitDetector::ExtraPath> &detected, bool unix_target)
    -> void {
    findCompilersImpl(detected, "gcc", "g++", unix_target);
    findCompilersImpl(detected, "clang", "clang++", unix_target);
}

////////////////////////////////////////////
// public API

auto findCompilers(bool unix_target) -> std::vector<ExtraPath> {
    auto detected = std::vector<ExtraPath>();
    findCppCompilersInPath(detected, unix_target);

#if defined(_WIN32)
    findCppCompilersWindows(detected);
#endif

    findRustSetup(detected, unix_target);
    return detected;
}

auto findQtVersions(bool unix_target, std::vector<ExtraPath> &detectedQt,
                    std::vector<ExtraPath> &detectedCompilers) -> void {
    auto knownLocations = std::vector<std::filesystem::path>{
        // clang-format off
        "/usr/local/Qt",
        "/opt/Qt",
        "c:\\qt\\"
        // clang-format on
    };

    const std::vector<std::string> qt6EnvVars = {
        // clang-format off
        "QTDIR",
        "QT_DIR",
        "QT6DIR",
        "QT6_DIR",
        // clang-format on
    };

    std::function<Toolchain(const std::filesystem::path &)> detectQtToolchain;
#if defined(_WIN32)
    detectQtToolchain = [](const std::filesystem::path &qtPath) -> Toolchain {
        auto lower = qtPath.string();
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        if (lower.find("mingw") != std::string::npos) {
            return Toolchain::MinGW;
        }
        if (lower.find("msvc") != std::string::npos) {
            return Toolchain::MSVC;
        }

        // A MinGW-built Qt ships its own runtime DLLs in bin/
        for (const auto &dll : {"libgcc_s_seh-1.dll", "libgcc_s_dw2-1.dll", "libstdc++-6.dll",
                                "libwinpthread-1.dll"}) {
            if (std::filesystem::exists(qtPath / "bin" / dll)) {
                return Toolchain::MinGW;
            }
        }

        // Fall back to the mkspec the kit was built with
        auto mkspecs = qtPath / "mkspecs";
        if (std::filesystem::exists(mkspecs / "win32-g++")) {
            return Toolchain::MinGW;
        }
        if (std::filesystem::exists(mkspecs / "win32-msvc")) {
            return Toolchain::MSVC;
        }
        return Toolchain::Unknown;
    };
#else
    detectQtToolchain = [](const std::filesystem::path &qtPath) -> Toolchain {
        auto lower = qtPath.string();
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(::tolower(c)); });
        if (lower.find("mingw") != std::string::npos) {
            return Toolchain::MinGW;
        }
        if (lower.find("msvc") != std::string::npos) {
            return Toolchain::MSVC;
        }
        // On Unix there is no MSVC/MinGW distinction, so the kit pairs with
        // any detected compiler (GCC, clang, ...).
        return Toolchain::Unknown;
    };
#endif

    for (const auto &envVar : qt6EnvVars) {
        auto envValue = safeGetEnv(envVar.c_str());
        if (!envValue.empty()) {
            knownLocations.push_back(std::filesystem::path(envValue));
        }
    }

    auto homeDirEnv = safeGetEnv(HOME_DIR_ENV);
    if (!homeDirEnv.empty()) {
        auto homedir = std::filesystem::path(homeDirEnv) / "qt";
        knownLocations.push_back(homedir);
    }

    auto path_env = safeGetEnv("PATH");
    auto ss = std::stringstream(path_env);
    auto dir = std::string();

    auto addDir = [&detectedQt, &detectQtToolchain, unix_target](const std::string &dir,
                                                                 std::string &name) {
        auto extraPath = ExtraPath();
        extraPath.name = std::string("Qt - ") + name;
        extraPath.compiler_path = dir;
        extraPath.toolchain = detectQtToolchain(std::filesystem::path(dir));
        if (unix_target) {
            extraPath.comment = "# qt installation";
            extraPath.command = "export QTDIR=%1\n";
            extraPath.command += "export QT_DIR=$QTDIR\n";
            extraPath.command += "export QT6_DIR=$QTDIR\n\n";
            extraPath.command += "# lets add qt to the path\n";
            extraPath.command += "export PATH=$QTDIR/bin:$PATH\n\n";

            extraPath.command += "# lets add qt to the LDD path\n";
            extraPath.command += "UNAME=\"$(uname)\"\n";
            extraPath.command += "if [ \"$UNAME\" = \"Darwin\" ]; then\n";
            extraPath.command += "  export DYLD_LIBRARY_PATH=\"$QT6_DIR/lib:$DYLD_LIBRARY_PATH\"\n";
            extraPath.command += "elif [ \"$UNAME\" = \"Linux\" ]; then\n";
            extraPath.command += "  export LD_LIBRARY_PATH=\"$QT6_DIR/lib:$LD_LIBRARY_PATH\"\n";
            extraPath.command += "fi\n";
        } else {
            extraPath.comment = "@REM qt installation";
            extraPath.command = "SET QTDIR=%1\n";
            extraPath.command += "SET QT_DIR=%1\n";
            extraPath.command += "SET QT6_DIR=%1\n\n";
            // Note that on windows, the DLLs are in the bin directory, so we need to add
            // that directory to the path, unlike unix - which needs LD_LIBRARY_PATH
            extraPath.command += "@REM lets add qt to the path\n";
            extraPath.command += "SET PATH=%QT6_DIR%\\bin;%PATH%\n";
        }
        replaceAll(extraPath.command, "%1", dir);
        replaceAll(extraPath.comment, "%1", dir);
        detectedQt.push_back(extraPath);
    };

    auto detectInstallToolchains = [unix_target, &detectedCompilers](std::filesystem::path dir) {
        // we got something like: c:\Qt\6.7.2
        auto tools_path = dir / "Tools";
        if (!std::filesystem::is_directory(tools_path)) {
            return;
        }

        for (const auto &subEntry : std::filesystem::directory_iterator(tools_path)) {
            if (!subEntry.is_directory()) {
                continue;
            }
            auto dirName = subEntry.path().filename().string();
            auto l = dirName.find("mingw");
            if (l == std::string::npos) {
                continue;
            }
            auto gcc_path = subEntry.path() / "bin" / "gcc.exe";
            if (!std::filesystem::exists(gcc_path)) {
                auto p = gcc_path.string();
                continue;
            }

            auto extraPath = ExtraPath();
            extraPath.name = "MinGW " + dirName;
            extraPath.compiler_path = dir.string();
            extraPath.toolchain = Toolchain::MinGW;
            if (unix_target) {
                // TODO
                // I am unsure what this part of the code does, as it makes no sense.
                // Maybe in a msys environment, or wsl... not dealing with that now.
                extraPath.comment = "# MingW installation from Qt (*)";
                extraPath.command += "export MINGW_DIR=%1\n";
                extraPath.command += "export PATH=\"${PATH};${MINGW_DIR}/bin\n";
            } else {
                extraPath.comment = "@rem MingW installation from Qt (*)";
                extraPath.command += "set MINGW_DIR=%1\n";
                extraPath.command += "set PATH=%PATH%;%MINGW_DIR%\\bin\n";
                extraPath.command += "set CC=x86_64-w64-mingw32-gcc.exe\n";
                extraPath.command += "set CXX=x86_64-w64-mingw32-g++.exe\n";
                extraPath.command += "set CMAKE_GENERATOR=MinGW Makefiles\n";
                extraPath.command += "set CMAKE_MAKE_PROGRAM=mingw32-make.exe\n";
            }
            replaceAll(extraPath.command, "%1", subEntry.path().string());
            detectedCompilers.push_back(extraPath);
        }
    };

    while (std::getline(ss, dir, ENV_SEPARATOR)) {
        auto qmake_path =
            std::filesystem::path(dir) / std::string("bin") / (std::string("qmake") + BINARY_EXT);
        if (!std::filesystem::exists(qmake_path)) {
            continue;
        }
        if (isCompilerAlreadyFound(detectedQt, dir)) {
            continue;
        }
        auto relativePath = std::filesystem::path(dir).parent_path().filename().string();
        addDir(dir, relativePath);
        detectInstallToolchains(dir);
    }

    for (const auto &root : knownLocations) {
        if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
            continue;
        }
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
            if (!entry.is_directory()) {
                continue;
            }
            if (!isValidQtInstallation(entry.path())) {
                continue;
            }
            auto relativePath = std::filesystem::relative(entry.path(), root).string();
            addDir(entry.path().string(), relativePath);
        }
        detectInstallToolchains(root);
    }
}

auto findCompilerTools(bool /*unix_target*/) -> std::vector<ExtraPath> {
    auto detected = std::vector<ExtraPath>();

#if defined(_WIN32)
    findCompilerToolsWindows(detected);
#endif

    return detected;
}

void generateKitFiles(const std::filesystem::path &directoryPath,
                      const std::vector<ExtraPath> &tools, const std::vector<ExtraPath> &compilers,
                      const std::vector<ExtraPath> &qtInstalls, bool unix_target) {
    auto kitNumber = 1;
    auto SCRIPT_EXTENSION = unix_target ? SCRIPT_EXTENSION_UNIX : SCRIPT_EXTENSION_WIN32;
    auto SCRIPT_HEADER = unix_target ? SCRIPT_HEADER_UNIX : SCRIPT_HEADER_WIN32;
    auto SCRIPT_SUFFIX = unix_target ? SCRIPT_SUFFIX_UNIX : SCRIPT_SUFFIX_WIN32;
    auto compilersSize = std::max<size_t>(1, compilers.size());
    auto qtinstallSize = std::max<size_t>(1, qtInstalls.size());

    // Only pair compilers with Qt installations that were built for the same
    // toolchain (e.g. an MSVC Qt build must not be matched with a MinGW
    // compiler). An Unknown toolchain on either side acts as a wildcard.
    struct CompilerQtPair {
        size_t compiler_index;
        size_t qt_index;
    };
    auto validPairs = std::vector<CompilerQtPair>();
    for (auto i = 0ul; i < compilersSize; ++i) {
        for (auto j = 0ul; j < qtinstallSize; ++j) {
            auto cc = compilers.empty() ? ExtraPath() : compilers[i];
            auto qtInst = qtInstalls.empty() ? ExtraPath() : qtInstalls[j];
            if (cc.toolchain == Toolchain::Unknown || qtInst.toolchain == Toolchain::Unknown ||
                cc.toolchain == qtInst.toolchain) {
                validPairs.push_back({i, j});
            }
        }
    }
    // If no pair matches at all, fall back to all combinations rather than
    // generating zero kits.
    if (validPairs.empty()) {
        for (auto i = 0ul; i < compilersSize; ++i) {
            for (auto j = 0ul; j < qtinstallSize; ++j) {
                validPairs.push_back({i, j});
            }
        }
    }

    for (const auto &pair : validPairs) {
        auto cc = compilers.empty() ? ExtraPath() : compilers[pair.compiler_index];
        auto qtInst = qtInstalls.empty() ? ExtraPath() : qtInstalls[pair.qt_index];

        auto scriptName =
            std::string("qtedit-kit-").append(std::to_string(kitNumber)).append(SCRIPT_EXTENSION);
        auto scriptPath = directoryPath / scriptName;
        std::ofstream scriptFile(scriptPath.string());
        if (!scriptFile) {
            std::cerr << "Error: Could not create script file at " << scriptPath << std::endl;
            continue;
        }

        auto scriptDisplay = std::string(SCRIPT_HEADER);
        replaceAll(scriptDisplay, "@@NAME@@", cc.name + ", " + qtInst.name + " *");

        scriptFile << scriptDisplay;
        for (auto &tt : tools) {
            scriptFile << tt.comment;
            scriptFile << "\n";
            scriptFile << tt.command;
            scriptFile << "\n";
        }

        scriptFile << "\n";
        if (!cc.command.empty()) {
            scriptFile << cc.comment;
            scriptFile << "\n";
            scriptFile << cc.command;
            scriptFile << "\n";
            scriptFile << "\n";
        }

        if (!qtInst.command.empty()) {
            scriptFile << qtInst.comment;
            scriptFile << "\n";
            scriptFile << qtInst.command;
            scriptFile << "\n";
            scriptFile << "\n";
        }
        scriptFile << SCRIPT_SUFFIX;
#if defined(__APPLE__) || defined(__MACH__) || defined(__unix__) || defined(__linux__)
        auto c_path = scriptPath.c_str();
        if (chmod(c_path, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
            std::perror("chmod failed");
        }
#endif
        kitNumber++;
    }
}

auto deleteOldKitFiles(const std::filesystem::path &directoryPath) -> void {
    // first delete older auto generated kits:
    std::filesystem::directory_iterator dirIt(directoryPath), endIt;
    for (; dirIt != endIt; ++dirIt) {
        auto fn = dirIt->path().filename().string();
        auto is_generated_kit = fn.find("qtedit-kit-");
        if (dirIt->is_regular_file() && is_generated_kit == 0) {
            try {
                std::filesystem::remove(dirIt->path());
            } catch (const std::filesystem::filesystem_error &e) {
                std::cerr << "Failed to delete: " << dirIt->path() << " - " << e.what()
                          << std::endl;
            }
        }
    }
}

} // namespace KitDetector
